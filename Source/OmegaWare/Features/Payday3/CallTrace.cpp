#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL && defined(_DEBUG)

#include "Core/Log.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Engines/Unreal/RpcValidate.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace OmegaWare
{
	namespace
	{
		using Clock_t = std::chrono::steady_clock;
		constexpr std::size_t kRingSize = 80;
		constexpr std::size_t kDumpSize = 40;
		constexpr std::chrono::milliseconds kStateInterval{ 500 };
		constexpr std::chrono::seconds kSummaryInterval{ 5 };
		constexpr std::uint32_t kLogFirstCalls = 5;
		constexpr std::uint32_t kLogEvery = 100;

		struct stRingEntry
		{
			Clock_t::time_point tpWhen;
			std::string strText;
		};

		std::mutex g_mtxTrace;
		std::deque<stRingEntry> g_aRing;
		std::unordered_map<SDK::UFunction*, std::uint32_t> g_mapRpcCounts;
		std::unordered_map<std::string, std::uint32_t> g_mapOtherCounts;

		[[nodiscard]] bool IsNetworkCall(const SDK::UFunction* pFunction)
		{
			const auto uFlags = static_cast<std::uint32_t>(pFunction->FunctionFlags);
			return (uFlags & static_cast<std::uint32_t>(SDK::EFunctionFlags::Net)) != 0;
		}

		void Observe(const Unreal::stModCall& rCall)
		{
			SDK::UFunction* pFunction = rCall.pFunction;
			if (!Memory::IsValidObjectPtr(pFunction))
				return;
			const bool bNetwork = IsNetworkCall(pFunction);
			std::string strFunction = pFunction->GetFullName();
			std::scoped_lock lckTrace(g_mtxTrace);
			if (!bNetwork)
			{
				++g_mapOtherCounts[strFunction];
				return;
			}
			const std::string strObject = Memory::IsValidObjectPtr(rCall.pObject) ? rCall.pObject->GetName() : std::string("?");
			const std::string strText = std::format("{} on {}", strFunction, strObject);
			g_aRing.push_back({ Clock_t::now(), strText });
			if (g_aRing.size() > kRingSize)
				g_aRing.pop_front();
			const std::uint32_t uCount = ++g_mapRpcCounts[pFunction];
			if (uCount <= kLogFirstCalls || uCount % kLogEvery == 0)
				LogInfo("calltrace: the mod called {} (call {})", strText, uCount);
		}

		[[nodiscard]] SDK::APlayerState* HostState()
		{
			SDK::AGameStateBase* pGameState = Unreal::GetGameState();
			if (!Payday3::IsAlive(pGameState))
				return nullptr;
			SDK::APlayerState* pHost = nullptr;
			SDK::TArray<SDK::APlayerState*>& aPlayers = pGameState->PlayerArray;
			for (int iPlayer = 0; iPlayer < aPlayers.Num(); ++iPlayer)
			{
				SDK::APlayerState* pState = aPlayers[iPlayer];
				if (Payday3::IsAlive(pState) && (!pHost || pState->PlayerId < pHost->PlayerId))
					pHost = pState;
			}
			return pHost;
		}

		class clCallTraceFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "calltrace"; }
			[[nodiscard]] std::string_view GetGroup() const override { return "debug"; }

			bool Setup() override
			{
				return true;
			}

			void Destroy() override
			{
				Unreal::SetModCallObserver(nullptr);
			}

			void Run() override
			{
				const auto tpNow = Clock_t::now();
				if (tpNow - m_tpState >= kStateInterval)
				{
					m_tpState = tpNow;
					UpdateState();
				}
				if (m_bTracing && tpNow - m_tpSummary >= kSummaryInterval)
				{
					m_tpSummary = tpNow;
					LogSummary();
				}
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				if (OmegaGUI::BeginSection("calltrace.section"_l))
				{
					OmegaGUI::TextMuted("calltrace.note"_t);
					OmegaGUI::TextMuted(std::format("{}: {}", "calltrace.status"_t, m_bTracing.load() ? "calltrace.on"_t : "calltrace.off"_t));
					OmegaGUI::TextMuted(m_strReason);
					if (OmegaGUI::Button("calltrace.dump"_l))
						Payday3::DumpCallTrace("dump requested from the menu");
					OmegaGUI::EndSection();
				}
			}

		private:
			void UpdateState()
			{
				std::string strReason;
				bool bWanted = false;
				const Unreal::ENetRole eRole = Unreal::GetNetRole();
				if (!Payday3::IsReady() || eRole == Unreal::ENetRole::Unknown)
					strReason = "waiting for a world";
				else if (eRole == Unreal::ENetRole::Solo)
					strReason = "off: solo mode, there is no host to kick you";
				else if (eRole == Unreal::ENetRole::Host)
					strReason = "off: you are hosting";
				else
				{
					SDK::APlayerState* pHost = HostState();
					if (!pHost)
						strReason = "off: the host is unknown";
					else if (Payday3::GetModUserKind(pHost) != Payday3::EModUserKind::None)
						strReason = std::format("off: {} (the host) uses OmegaWare", Payday3::PlayerName(pHost));
					else
					{
						bWanted = true;
						strReason = std::format("on: {} (the host) does not use OmegaWare", Payday3::PlayerName(pHost));
					}
				}
				if (bWanted != m_bTracing.load())
				{
					m_bTracing = bWanted;
					Unreal::SetModCallObserver(bWanted ? &Observe : nullptr);
					LogInfo("calltrace: {}", strReason);
					if (!bWanted)
					{
						std::scoped_lock lckTrace(g_mtxTrace);
						g_aRing.clear();
						g_mapRpcCounts.clear();
						g_mapOtherCounts.clear();
					}
				}
				m_strReason = std::move(strReason);
			}

			void LogSummary()
			{
				std::vector<std::pair<std::string, std::uint32_t>> aCalls;
				{
					std::scoped_lock lckTrace(g_mtxTrace);
					aCalls.assign(g_mapOtherCounts.begin(), g_mapOtherCounts.end());
					g_mapOtherCounts.clear();
				}
				if (aCalls.empty())
					return;
				std::ranges::sort(aCalls, [](const auto& rLeft, const auto& rRight) { return rLeft.second > rRight.second; });
				std::string strLine;
				for (std::size_t uIndex = 0; uIndex < std::min<std::size_t>(aCalls.size(), 10); ++uIndex)
					strLine += std::format("{}{} x{}", uIndex ? ", " : "", aCalls[uIndex].first, aCalls[uIndex].second);
				LogInfo("calltrace: game calls from the mod in the last {} s: {}", kSummaryInterval.count(), strLine);
			}

			std::atomic<bool> m_bTracing = false;
			std::string m_strReason = "waiting for a world";
			Clock_t::time_point m_tpState;
			Clock_t::time_point m_tpSummary;
		};

		clCallTraceFeature g_clCallTraceFeature;
	}

	namespace Payday3
	{
		void DumpCallTrace(std::string_view strReason)
		{
			std::vector<stRingEntry> aEntries;
			{
				std::scoped_lock lckTrace(g_mtxTrace);
				const std::size_t uStart = g_aRing.size() > kDumpSize ? g_aRing.size() - kDumpSize : 0;
				aEntries.assign(g_aRing.begin() + static_cast<std::ptrdiff_t>(uStart), g_aRing.end());
			}
			const auto tpNow = Clock_t::now();
			LogWarning("calltrace: {}, the last {} network calls the mod made, newest last:", strReason, aEntries.size());
			for (const stRingEntry& rEntry : aEntries)
				LogWarning("calltrace:   {} ms ago: {}", std::chrono::duration_cast<std::chrono::milliseconds>(tpNow - rEntry.tpWhen).count(), rEntry.strText);
		}
	}
}

#endif
