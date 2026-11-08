#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "Core/Log.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Engines/Unreal/RpcValidate.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#pragma warning(push)
#pragma warning(disable : 4005 4369 4309 4267 4244)
#include "SDK/Engine_parameters.hpp"
#pragma warning(pop)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>

namespace OmegaWare
{
	namespace
	{
		using Clock_t = std::chrono::steady_clock;
		constexpr std::chrono::milliseconds kPollInterval{ 250 };
		constexpr std::chrono::seconds kCauseWindow{ 6 };
		constexpr std::chrono::seconds kNetworkErrorWindow{ 4 };

		struct stKnownPlayer
		{
			std::string strName;
			bool bLocal = false;
		};

		struct stCause
		{
			std::string strText;
			Clock_t::time_point tpWhen;
		};

		[[nodiscard]] std::string_view FailureName(SDK::ENetworkFailure eFailure)
		{
			switch (eFailure)
			{
			case SDK::ENetworkFailure::NetDriverAlreadyExists: return "net driver already exists";
			case SDK::ENetworkFailure::NetDriverCreateFailure: return "net driver could not be created";
			case SDK::ENetworkFailure::NetDriverListenFailure: return "net driver could not listen";
			case SDK::ENetworkFailure::ConnectionLost: return "connection lost";
			case SDK::ENetworkFailure::ConnectionTimeout: return "connection timed out";
			case SDK::ENetworkFailure::FailureReceived: return "failure received from the other side";
			case SDK::ENetworkFailure::OutdatedClient: return "outdated client";
			case SDK::ENetworkFailure::OutdatedServer: return "outdated server";
			case SDK::ENetworkFailure::PendingConnectionFailure: return "pending connection failed";
			case SDK::ENetworkFailure::NetGuidMismatch: return "net guid mismatch";
			case SDK::ENetworkFailure::NetChecksumMismatch: return "net checksum mismatch";
			default: return "unknown failure";
			}
		}

		class clNetLogFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "netlog"; }
			[[nodiscard]] bool HasMenu() const override { return false; }

			bool Setup() override
			{
				m_hKick = Unreal::OnFunction<SDK::Params::PlayerController_ClientWasKicked>("Engine.PlayerController.ClientWasKicked",
					[this](SDK::UObject* pObject, SDK::Params::PlayerController_ClientWasKicked& rParams, bool&) {
						const std::string strReason = rParams.KickReason.ToString();
						const std::string strShown = strReason.empty() ? std::string("no reason given") : strReason;
						auto* pController = static_cast<SDK::APlayerController*>(pObject);
						SDK::APlayerState* pState = Payday3::IsAlive(pController) ? pController->PlayerState : nullptr;
						LogInfo("net: the server kicked {}: {}", NameOf(pState), strShown);
							Payday3::DumpCallTrace(std::format("kicked by the server: {}", strShown));
						NoteCause(pState, std::format("kicked by the server ({})", strShown));
					});
				m_hError = Unreal::OnFunction<SDK::Params::GameInstance_HandleNetworkError>("Engine.GameInstance.HandleNetworkError",
					[this](SDK::UObject*, SDK::Params::GameInstance_HandleNetworkError& rParams, bool&) {
						const std::string strFailure(FailureName(rParams.FailureType));
						LogWarning("net: network failure on the {}: {}", rParams.bIsServer ? "host side" : "client side", strFailure);
							Payday3::DumpCallTrace(std::format("network failure: {}", strFailure));
						std::scoped_lock lckCauses(m_mtxCauses);
						m_stNetworkError = { strFailure, Clock_t::now() };
					});
				return true;
			}

			void Destroy() override
			{
				if (m_hKick != Unreal::kInvalidHook)
					Unreal::RemoveProcessEvent(m_hKick);
				if (m_hError != Unreal::kInvalidHook)
					Unreal::RemoveProcessEvent(m_hError);
				m_hKick = Unreal::kInvalidHook;
				m_hError = Unreal::kInvalidHook;
			}

			void Run() override
			{
				const auto tpNow = Clock_t::now();
				if (tpNow < m_tpNextPoll || !Payday3::IsReady() || m_bQueued.exchange(true))
					return;
				m_tpNextPoll = tpNow + kPollInterval;
				if (!Unreal::RunOnGameThread([this] {
						Features::Guard(*this, "game thread", [this] { Poll(); });
						m_bQueued = false;
					}))
					m_bQueued = false;
			}

			void NoteCause(const SDK::APlayerState* pState, std::string strText)
			{
				if (!pState)
					return;
				std::scoped_lock lckCauses(m_mtxCauses);
				m_mapCauses[pState] = { std::move(strText), Clock_t::now() };
			}

		private:
			static std::string NameOf(SDK::APlayerState* pState)
			{
				return Payday3::IsAlive(pState) ? pState->GetPlayerName().ToString() : std::string("an unknown player");
			}

			void Poll()
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::AGameStateBase* pGameState = Unreal::GetGameState();
				if (!pWorld || !Payday3::IsAlive(pGameState))
					return;
				SDK::APlayerState* pLocalState = nullptr;
				if (SDK::APlayerController* pController = Unreal::GetPlayerController())
					pLocalState = pController->PlayerState;

				std::unordered_map<const SDK::APlayerState*, stKnownPlayer> mapNow;
				SDK::TArray<SDK::APlayerState*>& aPlayers = pGameState->PlayerArray;
				for (int iPlayer = 0; iPlayer < aPlayers.Num(); ++iPlayer)
				{
					SDK::APlayerState* pState = aPlayers[iPlayer];
					if (Payday3::IsAlive(pState))
						mapNow.emplace(pState, stKnownPlayer{ pState->GetPlayerName().ToString(), pState == pLocalState });
				}
				if (pWorld != m_pWorld)
				{
					m_pWorld = pWorld;
					m_bWasHost = false;
					m_mapKnown = std::move(mapNow);
					std::scoped_lock lckCauses(m_mtxCauses);
					m_mapCauses.clear();
					return;
				}
				const bool bHost = Unreal::IsNetHost();
				if (bHost)
					m_bWasHost = true;
				const bool bEveryoneGone = std::ranges::none_of(mapNow, [](const auto& rEntry) { return !rEntry.second.bLocal; });
				for (const auto& [pState, rPlayer] : mapNow)
					if (!m_mapKnown.contains(pState) && !rPlayer.bLocal)
						LogInfo("net: {} joined", rPlayer.strName);
				for (const auto& [pState, rPlayer] : m_mapKnown)
				{
					if (mapNow.contains(pState) || rPlayer.bLocal)
						continue;
					LogInfo("net: {} left, {}", rPlayer.strName, DescribeLeave(pState, bHost, bEveryoneGone && m_bWasHost));
				}
				m_mapKnown = std::move(mapNow);
			}

			std::string DescribeLeave(const SDK::APlayerState* pState, bool bHost, bool bLobbyClosed)
			{
				const auto tpNow = Clock_t::now();
				std::scoped_lock lckCauses(m_mtxCauses);
				if (const auto itCause = m_mapCauses.find(pState); itCause != m_mapCauses.end())
				{
					const stCause stFound = itCause->second;
					m_mapCauses.erase(itCause);
					if (tpNow - stFound.tpWhen <= kCauseWindow)
						return stFound.strText;
				}
				if (!m_stNetworkError.strText.empty() && tpNow - m_stNetworkError.tpWhen <= kNetworkErrorWindow)
					return "network failure: " + m_stNetworkError.strText;
				if (bLobbyClosed)
					return "the lobby closed because you left or ended the session";
				if (bHost)
					return "no kick, rejected network call or network failure was seen, so this was a normal leave, a timeout or a crash on their side";
				return "the host or the connection ended the session without a recorded reason";
			}

			Unreal::HookHandle m_hKick = Unreal::kInvalidHook;
			Unreal::HookHandle m_hError = Unreal::kInvalidHook;
			std::atomic<bool> m_bQueued = false;
			Clock_t::time_point m_tpNextPoll;
			SDK::UWorld* m_pWorld = nullptr;
			bool m_bWasHost = false;
			std::unordered_map<const SDK::APlayerState*, stKnownPlayer> m_mapKnown;
			std::mutex m_mtxCauses;
			std::unordered_map<const SDK::APlayerState*, stCause> m_mapCauses;
			stCause m_stNetworkError;
		};

		clNetLogFeature g_clNetLogFeature;
	}

	namespace Payday3
	{
		void NoteDisconnectCause(const SDK::APlayerState* pState, const std::string& strCause)
		{
			g_clNetLogFeature.NoteCause(pState, strCause);
		}
	}
}

#endif
