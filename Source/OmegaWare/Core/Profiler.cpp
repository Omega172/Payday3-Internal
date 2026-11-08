#include "Core/Profiler.hpp"

#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>

#pragma comment(lib, "psapi.lib")

namespace OmegaWare::Profiler
{
	namespace
	{
		struct stAccumulator
		{
			std::uint64_t uCalls = 0;
			std::int64_t iTotalTicks = 0;
			std::int64_t iMaxTicks = 0;
		};

		struct stHash
		{
			using is_transparent = void;
			std::size_t operator()(std::string_view strName) const { return std::hash<std::string_view>{}(strName); }
			std::size_t operator()(const std::string& strName) const { return std::hash<std::string_view>{}(strName); }
		};

		std::mutex g_mtxStats;
		std::unordered_map<std::string, stAccumulator, stHash, std::equal_to<>> g_mapStats;
		std::atomic<std::uint64_t> g_uProcessEvents = 0;

		[[nodiscard]] double TicksToMs(std::int64_t iTicks)
		{
			static const double s_dMsPerTick = [] {
				LARGE_INTEGER stFrequency{};
				QueryPerformanceFrequency(&stFrequency);
				return 1000.0 / static_cast<double>(stFrequency.QuadPart);
			}();
			return static_cast<double>(iTicks) * s_dMsPerTick;
		}
	}

	std::int64_t Now()
	{
		LARGE_INTEGER stCounter{};
		QueryPerformanceCounter(&stCounter);
		return stCounter.QuadPart;
	}

	void Record(std::string_view strName, std::int64_t iTicks)
	{
		std::scoped_lock lckStats(g_mtxStats);
		auto itStat = g_mapStats.find(strName);
		if (itStat == g_mapStats.end())
			itStat = g_mapStats.emplace(std::string(strName), stAccumulator{}).first;
		stAccumulator& rStat = itStat->second;
		++rStat.uCalls;
		rStat.iTotalTicks += iTicks;
		rStat.iMaxTicks = std::max(rStat.iMaxTicks, iTicks);
	}

	std::vector<stStat> Snapshot(bool bReset)
	{
		std::vector<stStat> aStats;
		std::scoped_lock lckStats(g_mtxStats);
		aStats.reserve(g_mapStats.size());
		for (auto& [strName, rStat] : g_mapStats)
			aStats.push_back({ strName, rStat.uCalls, TicksToMs(rStat.iTotalTicks), TicksToMs(rStat.iMaxTicks) });
		if (bReset)
			g_mapStats.clear();
		std::ranges::sort(aStats, [](const stStat& rLeft, const stStat& rRight) { return rLeft.dTotalMs > rRight.dTotalMs; });
		return aStats;
	}

	void CountProcessEvent()
	{
		g_uProcessEvents.fetch_add(1, std::memory_order_relaxed);
	}

	std::uint64_t TakeProcessEventCount()
	{
		return g_uProcessEvents.exchange(0, std::memory_order_relaxed);
	}

	stProcessSample SampleProcess(bool bWithThreads)
	{
		stProcessSample stSample;
		PROCESS_MEMORY_COUNTERS_EX stMemory{};
		stMemory.cb = sizeof(stMemory);
		if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&stMemory), sizeof(stMemory)))
		{
			stSample.dPrivateMb = static_cast<double>(stMemory.PrivateUsage) / (1024. * 1024.);
			stSample.dWorkingMb = static_cast<double>(stMemory.WorkingSetSize) / (1024. * 1024.);
		}
		DWORD uHandles = 0;
		if (GetProcessHandleCount(GetCurrentProcess(), &uHandles))
			stSample.uHandles = uHandles;
		stSample.uGdiObjects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
		stSample.uUserObjects = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
		if (const HANDLE hSnapshot = bWithThreads ? CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0) : INVALID_HANDLE_VALUE; hSnapshot != INVALID_HANDLE_VALUE)
		{
			const DWORD uProcess = GetCurrentProcessId();
			THREADENTRY32 stEntry{ .dwSize = sizeof(THREADENTRY32) };
			for (BOOL bMore = Thread32First(hSnapshot, &stEntry); bMore; bMore = Thread32Next(hSnapshot, &stEntry))
				if (stEntry.th32OwnerProcessID == uProcess)
					++stSample.uThreads;
			CloseHandle(hSnapshot);
		}
		return stSample;
	}
}
