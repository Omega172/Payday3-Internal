#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace OmegaWare::Profiler
{
	struct stStat
	{
		std::string strName;
		std::uint64_t uCalls = 0;
		double dTotalMs = 0.;
		double dMaxMs = 0.;
	};

	struct stProcessSample
	{
		double dPrivateMb = 0.;
		double dWorkingMb = 0.;
		std::uint32_t uHandles = 0;
		std::uint32_t uGdiObjects = 0;
		std::uint32_t uUserObjects = 0;
		std::uint32_t uThreads = 0;
	};

	void Record(std::string_view strName, std::int64_t iTicks);
	[[nodiscard]] std::vector<stStat> Snapshot(bool bReset);
	[[nodiscard]] std::int64_t Now();
	void CountProcessEvent();
	[[nodiscard]] std::uint64_t TakeProcessEventCount();
	[[nodiscard]] stProcessSample SampleProcess(bool bWithThreads = false);

	class clScope
	{
	public:
		explicit clScope(std::string_view strName) : m_strName(strName), m_iStart(Now()) {}
		~clScope() { Record(m_strName, Now() - m_iStart); }

		clScope(const clScope&) = delete;
		clScope& operator=(const clScope&) = delete;

	private:
		std::string_view m_strName;
		std::int64_t m_iStart;
	};
}
