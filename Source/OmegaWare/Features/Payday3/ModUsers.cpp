#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "Config/Config.hpp"
#include "Core/Log.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#include "SDK/Starbreeze_parameters.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OmegaWare::Payday3
{
	namespace
	{
		constexpr std::wstring_view kMarker = L"​‌‍‌​⁠";

		constexpr std::array<std::string_view, 2> kDeveloperIds = { "e2fe86ccda6142cf8a146a3ce77fbd2f", "000269aa76594f42af4b8d83f6945468" };

		std::string IdsOf(SDK::APlayerState* pState)
		{
			if (!Memory::IsValidObjectPtr(pState) || !pState->IsA(SDK::ASBZPlayerState::StaticClass()))
				return {};
			auto* pSbzState = static_cast<SDK::ASBZPlayerState*>(pState);
			return std::format("accelbyte {} eos {}", pSbzState->AccelByteUserId.ToString(), pSbzState->EOSProductUserId.ToString());
		}

		[[nodiscard]] bool IsDeveloperId(SDK::APlayerState* pState)
		{
			if (!Memory::IsValidObjectPtr(pState) || !pState->IsA(SDK::ASBZPlayerState::StaticClass()))
				return false;
			auto* pSbzState = static_cast<SDK::ASBZPlayerState*>(pState);
			const std::string strAccelByte = pSbzState->AccelByteUserId.ToString();
			const std::string strEos = pSbzState->EOSProductUserId.ToString();
			return std::ranges::any_of(kDeveloperIds, [&](std::string_view strId) { return !strId.empty() && (strId == strAccelByte || strId == strEos); });
		}

		std::mutex g_mtxModUsers;
		std::unordered_set<std::string> g_setDevelopers;
		std::unordered_set<std::string> g_setModUsers;
		std::vector<std::string> g_aNewModUsers;

		class clModUsersFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "modusers"; }
			[[nodiscard]] bool HasMenu() const override { return false; }

			bool Setup() override
			{
				m_pBroadcast = &Config::Add("modusers.broadcast", false);
				m_pDetect = &Config::Add("modusers.detect", true);
				m_pToast = &Config::Add("modusers.toast", true);
				m_pHide = &Config::Add("modusers.hide", true);
				m_hReceive = Unreal::OnFunction<SDK::Params::SBZChatInGame_MulticastChatMessageReceived>("Starbreeze.SBZChatInGame.MulticastChatMessageReceived",
					[this](SDK::UObject*, SDK::Params::SBZChatInGame_MulticastChatMessageReceived& rParams, bool& bSkip) { OnChat(rParams.ChatEventData, bSkip); });
				return true;
			}

			void Destroy() override
			{
				if (m_hReceive != Unreal::kInvalidHook)
					Unreal::RemoveProcessEvent(m_hReceive);
				m_hReceive = Unreal::kInvalidHook;
			}

			void Render() override
			{
				std::vector<std::string> aFound;
				{
					std::scoped_lock lckUsers(g_mtxModUsers);
					aFound.swap(g_aNewModUsers);
				}
				if (!*m_pToast)
					return;
				for (const std::string& strName : aFound)
					OmegaGUI::PushToast(OmegaGUI::EToastKind::Info, OmegaLocalization::Format("modusers.found", strName));
			}

			void Run() override
			{
				if (!*m_pBroadcast || !Payday3::IsReady() || m_bQueued.exchange(true))
					return;
				if (!Unreal::RunOnGameThread([this] {
						m_bQueued = false;
						Features::Guard(*this, "game thread", [this] { Announce(); });
					}))
					m_bQueued = false;
			}

			void DrawMenu() override
			{
				DrawSection();
			}

			void DrawSection()
			{
				using namespace OmegaLocalization::Literals;
				if (OmegaGUI::BeginSection("modusers.section"_l))
				{
					OmegaGUI::Checkbox("modusers.broadcast"_l, m_pBroadcast);
					OmegaGUI::ItemTooltip("modusers.broadcast_hint"_t);
					OmegaGUI::Checkbox("modusers.detect"_l, m_pDetect);
					OmegaGUI::ItemTooltip("modusers.detect_hint"_t);
					if (*m_pDetect)
					{
						OmegaGUI::Checkbox("modusers.hide"_l, m_pHide);
						OmegaGUI::ItemTooltip("modusers.hide_hint"_t);
						OmegaGUI::Checkbox("modusers.toast"_l, m_pToast);
					}
					{
						std::scoped_lock lckUsers(g_mtxModUsers);
						OmegaGUI::TextMuted(OmegaLocalization::Format("modusers.count", g_setModUsers.size()));
					}
					OmegaGUI::EndSection();
				}
			}

		public:
			bool SendRawPublic(const std::wstring& wstrText) { return SendRaw(wstrText); }

		private:
			void OnChat(SDK::FSBZPlayerChatEvent& rEvent, bool& bSkip)
			{
				const std::wstring wstrText = rEvent.Message.ToWString();
				if (Payday3::HandleSpawnMessage(rEvent.PlayerState, wstrText))
				{
					bSkip = true;
					return;
				}
				if (!*m_pDetect)
					return;
				const std::string strSender = Memory::IsValidObjectPtr(rEvent.PlayerState) ? rEvent.PlayerState->GetPlayerName().ToString() : std::string();
				if (m_iChatLogged < 12)
				{
					++m_iChatLogged;
					LogInfo("omegaware users: chat from {} length {} first {:#x}", strSender, wstrText.size(), wstrText.empty() ? 0u : static_cast<unsigned>(wstrText.front()));
				}
				if (!wstrText.starts_with(kMarker) && !(wstrText.empty() && !strSender.empty()))
					return;
				bSkip = *m_pHide;
				const std::string& strName = strSender;
				if (strName.empty() || strName == m_strLocalName)
					return;
				const bool bDeveloper = IsDeveloperId(rEvent.PlayerState);
				std::scoped_lock lckUsers(g_mtxModUsers);
				if (bDeveloper)
					g_setDevelopers.insert(strName);
				if (g_setModUsers.insert(strName).second)
				{
					g_aNewModUsers.push_back(strName);
					LogInfo("omegaware users: {} is using OmegaWare ({})", strName, IdsOf(rEvent.PlayerState));
				}
			}

			bool SendRaw(const std::wstring& wstrText)
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::APawn* pLocalPawn = Unreal::GetLocalPawn();
				if (!pWorld || !Memory::IsValidObjectPtr(pLocalPawn) || !Memory::IsValidObjectPtr(pLocalPawn->PlayerState))
					return false;
				if (!Payday3::IsAlive(m_pChat))
				{
					m_pChat = nullptr;
					SDK::TArray<SDK::AActor*> aChats;
					SDK::UGameplayStatics::GetAllActorsOfClass(pWorld, SDK::ASBZChatInGame::StaticClass(), &aChats);
					if (aChats.Num() > 0)
						m_pChat = static_cast<SDK::ASBZChatInGame*>(aChats[0]);
				}
				if (!m_pChat)
					return false;
				SDK::FSBZPlayerChatEvent stEvent{};
				stEvent.PlayerState = pLocalPawn->PlayerState;
				stEvent.Message = SDK::FString(wstrText.c_str());
				m_pChat->SendChatMessageToServer(stEvent);
				return true;
			}

			void Announce()
			{
				using Clock_t = std::chrono::steady_clock;
				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::AGameStateBase* pGameState = Unreal::GetGameState();
				SDK::APawn* pLocalPawn = Unreal::GetLocalPawn();
				if (!pWorld || !Memory::IsValidObjectPtr(pGameState) || !Memory::IsValidObjectPtr(pLocalPawn))
					return;
				SDK::APlayerState* pLocalState = pLocalPawn->PlayerState;
				if (!Memory::IsValidObjectPtr(pLocalState))
					return;
				m_strLocalName = pLocalState->GetPlayerName().ToString();

				if (pWorld != m_pWorld)
				{
					m_pWorld = pWorld;
					m_iLastCount = 0;
					m_pChat = nullptr;
					LogInfo("omegaware users: you are {} ({})", m_strLocalName, IdsOf(pLocalState));
					std::scoped_lock lckUsers(g_mtxModUsers);
					g_setModUsers.clear();
					g_setDevelopers.clear();
				}
				const int iCount = pGameState->PlayerArray.Num();
				const Clock_t::time_point tpNow = Clock_t::now();
				const bool bNewPlayers = iCount > m_iLastCount;
				m_iLastCount = iCount;
				if (iCount < 2 || (!bNewPlayers && tpNow < m_tpNext) || tpNow < m_tpEarliest)
					return;

				if (!Payday3::IsAlive(m_pChat))
				{
					m_pChat = nullptr;
					if (tpNow < m_tpNextSearch)
						return;
					m_tpNextSearch = tpNow + std::chrono::seconds(2);
					SDK::TArray<SDK::AActor*> aChats;
					SDK::UGameplayStatics::GetAllActorsOfClass(pWorld, SDK::ASBZChatInGame::StaticClass(), &aChats);
					if (aChats.Num() > 0)
						m_pChat = static_cast<SDK::ASBZChatInGame*>(aChats[0]);
					LogInfo("omegaware users: found {} in game chat actors", aChats.Num());
				}
				if (!m_pChat)
					return;

				SDK::FSBZPlayerChatEvent stEvent{};
				stEvent.PlayerState = pLocalState;
				stEvent.Message = SDK::FString(std::wstring(kMarker).c_str());
				m_pChat->SendChatMessageToServer(stEvent);
				LogInfo("omegaware users: sent marker as {} to {} players", m_strLocalName, iCount);
				m_tpEarliest = tpNow + std::chrono::seconds(3);
				m_tpNext = tpNow + std::chrono::seconds(45);
			}

			bool* m_pBroadcast = nullptr;
			bool* m_pDetect = nullptr;
			bool* m_pToast = nullptr;
			bool* m_pHide = nullptr;
			Unreal::HookHandle m_hReceive = Unreal::kInvalidHook;
			std::atomic<bool> m_bQueued = false;
			SDK::UWorld* m_pWorld = nullptr;
			SDK::ASBZChatInGame* m_pChat = nullptr;
			std::chrono::steady_clock::time_point m_tpNextSearch;
			std::string m_strLocalName;
			int m_iLastCount = 0;
			std::atomic<int> m_iChatLogged = 0;
			std::chrono::steady_clock::time_point m_tpNext;
			std::chrono::steady_clock::time_point m_tpEarliest;
		};

		clModUsersFeature g_clModUsersFeature;
	}

	void DrawModUsersMenu()
	{
		g_clModUsersFeature.DrawSection();
	}

	bool SendModChat(const std::wstring& wstrText)
	{
		return g_clModUsersFeature.SendRawPublic(wstrText);
	}

	std::size_t ModUserCount()
	{
		std::scoped_lock lckUsers(g_mtxModUsers);
		return g_setModUsers.size();
	}

	namespace
	{
		constexpr std::chrono::milliseconds kNameRefresh{ 500 };
		constexpr std::size_t kNameCacheLimit = 128;

		struct stCachedPlayer
		{
			std::string strName;
			EModUserKind eKind = EModUserKind::None;
			std::chrono::steady_clock::time_point tpRefreshed;
		};

		std::mutex g_mtxNames;
		std::unordered_map<const SDK::APlayerState*, stCachedPlayer> g_mapNames;

		[[nodiscard]] stCachedPlayer LookupPlayer(const SDK::APlayerState* pState)
		{
			if (!Memory::IsValidObjectPtr(pState))
				return {};
			const auto tpNow = std::chrono::steady_clock::now();
			{
				std::scoped_lock lckNames(g_mtxNames);
				if (const auto itCached = g_mapNames.find(pState); itCached != g_mapNames.end() && tpNow - itCached->second.tpRefreshed < kNameRefresh)
					return itCached->second;
			}
			stCachedPlayer stFresh;
			stFresh.tpRefreshed = tpNow;
			const SDK::FString& rName = pState->PlayerNamePrivate;
			if (rName.Num() > 0 && rName.Num() < 256 && Memory::IsReadable(rName.CStr(), static_cast<std::size_t>(rName.Num()) * sizeof(wchar_t)))
				stFresh.strName = rName.ToString();
			{
				std::scoped_lock lckUsers(g_mtxModUsers);
				if (g_setModUsers.contains(stFresh.strName))
					stFresh.eKind = g_setDevelopers.contains(stFresh.strName) ? EModUserKind::Developer : EModUserKind::User;
			}
			std::scoped_lock lckNames(g_mtxNames);
			if (g_mapNames.size() >= kNameCacheLimit)
				g_mapNames.clear();
			g_mapNames[pState] = stFresh;
			return stFresh;
		}
	}

	EModUserKind GetModUserKind(const SDK::APlayerState* pState)
	{
		return LookupPlayer(pState).eKind;
	}

	std::string PlayerName(const SDK::APlayerState* pState)
	{
		return LookupPlayer(pState).strName;
	}
}

#endif
