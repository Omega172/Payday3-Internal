#include "Config/Config.hpp"
#include "Core/Log.hpp"
#include "Core/Profiler.hpp"
#include "Features/Feature.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <chrono>
#include <format>
#include <mutex>
#include <vector>

namespace OmegaWare
{
	namespace
	{
		constexpr std::chrono::seconds kWindow{ 5 };
		constexpr std::chrono::seconds kProcessLogInterval{ 60 };
		constexpr double kSlowCostMsPerSecond = 1.0;
		constexpr double kSlowMaxMs = 12.0;
		constexpr std::size_t kLoggedScopes = 5;

		class clProfilerFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "profiler"; }
			[[nodiscard]] std::string_view GetGroup() const override { return "debug"; }

			bool Setup() override
			{
				m_pLog = &Config::Add("profiler.log", true);
				const auto tpNow = std::chrono::steady_clock::now();
				m_tpWindow = tpNow;
				m_tpProcessLog = tpNow;
				m_stFirst = Profiler::SampleProcess();
				m_stProcess = m_stFirst;
				return true;
			}

			void Run() override
			{
				const auto tpNow = std::chrono::steady_clock::now();
				if (tpNow - m_tpWindow < kWindow)
					return;
				const double dSeconds = std::chrono::duration<double>(tpNow - m_tpWindow).count();
				m_tpWindow = tpNow;
				std::vector<Profiler::stStat> aStats = Profiler::Snapshot(true);
				const bool bProcessLog = tpNow - m_tpProcessLog >= kProcessLogInterval;
				const Profiler::stProcessSample stProcess = Profiler::SampleProcess(bProcessLog);
				const double dEvents = static_cast<double>(Profiler::TakeProcessEventCount()) / dSeconds;
				auto pRows = std::make_shared<std::vector<std::array<std::string, 5>>>();
				pRows->reserve(aStats.size());
				for (const Profiler::stStat& rStat : aStats)
					pRows->push_back({ rStat.strName, std::format("{:.0f}", static_cast<double>(rStat.uCalls) / dSeconds), std::format("{:.0f}", rStat.dTotalMs * 1000. / static_cast<double>(std::max<std::uint64_t>(rStat.uCalls, 1))), std::format("{:.1f}", rStat.dMaxMs), std::format("{:.2f}", rStat.dTotalMs / dSeconds) });
				{
					std::scoped_lock lckWindow(m_mtxWindow);
					m_pRows = std::move(pRows);
					m_dWindowSeconds = dSeconds;
					m_stProcess = stProcess;
					m_dEventsPerSecond = dEvents;
				}
				if (!*m_pLog)
					return;
				std::size_t uLogged = 0;
				for (const Profiler::stStat& rStat : aStats)
				{
					const double dCost = rStat.dTotalMs / dSeconds;
					if (uLogged >= kLoggedScopes || (dCost < kSlowCostMsPerSecond && rStat.dMaxMs < kSlowMaxMs))
						continue;
					LogWarning("profiler: {} ran {:.0f} times a second, {:.0f} us on average, {:.1f} ms at worst, {:.2f} ms of every second", rStat.strName, static_cast<double>(rStat.uCalls) / dSeconds, rStat.dTotalMs * 1000. / static_cast<double>(std::max<std::uint64_t>(rStat.uCalls, 1)), rStat.dMaxMs, dCost);
					++uLogged;
				}
				if (bProcessLog)
				{
					m_tpProcessLog = tpNow;
					LogInfo("profiler: private memory {:.0f} MB ({:+.0f} since start), working set {:.0f} MB, handles {} ({:+}), gdi {} user {}, threads {}, {:.0f} process event calls a second", stProcess.dPrivateMb, stProcess.dPrivateMb - m_stFirst.dPrivateMb, stProcess.dWorkingMb, stProcess.uHandles,
						static_cast<int>(stProcess.uHandles) - static_cast<int>(m_stFirst.uHandles), stProcess.uGdiObjects, stProcess.uUserObjects, stProcess.uThreads, dEvents);
				}
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				std::shared_ptr<const std::vector<std::array<std::string, 5>>> pRows;
				Profiler::stProcessSample stProcess;
				double dEvents = 0.;
				{
					std::scoped_lock lckWindow(m_mtxWindow);
					pRows = m_pRows;
					stProcess = m_stProcess;
					dEvents = m_dEventsPerSecond;
				}
				if (OmegaGUI::BeginSection("profiler.section"_l))
				{
					OmegaGUI::Checkbox("profiler.log"_l, m_pLog);
					OmegaGUI::ItemTooltip("profiler.log_hint"_t);
					OmegaGUI::TextMuted(std::format("{} {:.0f} MB ({:+.0f}), {} {} ({:+}), gdi {} user {}, {} {}", "profiler.memory"_t, stProcess.dPrivateMb, stProcess.dPrivateMb - m_stFirst.dPrivateMb, "profiler.handles"_t, stProcess.uHandles,
						static_cast<int>(stProcess.uHandles) - static_cast<int>(m_stFirst.uHandles), stProcess.uGdiObjects, stProcess.uUserObjects, "profiler.threads"_t, stProcess.uThreads));
					OmegaGUI::TextMuted(std::format("{}: {:.0f}", "profiler.events"_t, dEvents));
					if (OmegaGUI::BeginTable("##profiler.table", 5, OmegaGUI::ETableFlags::RowBackground))
					{
						OmegaGUI::TableSetupColumn("profiler.scope"_t, OmegaGUI::ETableColumnFlags::None, 2.4f);
						OmegaGUI::TableSetupColumn("profiler.calls"_t, OmegaGUI::ETableColumnFlags::None, 1.f);
						OmegaGUI::TableSetupColumn("profiler.avg"_t, OmegaGUI::ETableColumnFlags::None, 1.f);
						OmegaGUI::TableSetupColumn("profiler.max"_t, OmegaGUI::ETableColumnFlags::None, 1.f);
						OmegaGUI::TableSetupColumn("profiler.cost"_t, OmegaGUI::ETableColumnFlags::None, 1.f);
						OmegaGUI::TableNextRow();
						for (const char* szKey : { "profiler.scope_desc", "profiler.calls_desc", "profiler.avg_desc", "profiler.max_desc", "profiler.cost_desc" })
						{
							OmegaGUI::TableNextColumn();
							OmegaGUI::TextMuted(OmegaLocalization::Get(szKey));
						}
						if (pRows)
							for (const auto& rRow : *pRows)
							{
								OmegaGUI::TableNextRow();
								for (const std::string& strCell : rRow)
								{
									OmegaGUI::TableNextColumn();
									OmegaGUI::TextUnformatted(strCell);
								}
							}
						OmegaGUI::EndTable();
					}
					OmegaGUI::EndSection();
				}
			}

		private:
			bool* m_pLog = nullptr;
			std::chrono::steady_clock::time_point m_tpWindow;
			std::chrono::steady_clock::time_point m_tpProcessLog;
			Profiler::stProcessSample m_stFirst;
			std::mutex m_mtxWindow;
			std::shared_ptr<const std::vector<std::array<std::string, 5>>> m_pRows;
			Profiler::stProcessSample m_stProcess;
			double m_dWindowSeconds = 1.;
			double m_dEventsPerSecond = 0.;
		};

		clProfilerFeature g_clProfilerFeature;
	}
}
