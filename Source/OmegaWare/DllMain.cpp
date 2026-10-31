#include "Core/Framework.hpp"

#include <cstdio>

namespace
{
	HANDLE g_hInstanceGuard = nullptr;

	bool ClaimInstance()
	{
		wchar_t wszName[64];
		swprintf_s(wszName, L"Local\\OmegaWare.Instance.%lu", GetCurrentProcessId());
		g_hInstanceGuard = CreateMutexW(nullptr, FALSE, wszName);
		if (g_hInstanceGuard && GetLastError() == ERROR_ALREADY_EXISTS)
		{
			CloseHandle(g_hInstanceGuard);
			g_hInstanceGuard = nullptr;
			return false;
		}
		return true;
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD uReason, LPVOID)
{
	if (uReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		if (OMEGAWARE_PROXY && GetEnvironmentVariableW(L"OMEGAWARE_DISABLE", nullptr, 0) > 0)
			return TRUE;
		if (!ClaimInstance())
			return TRUE;
		OmegaWare::Start(hModule);
	}
	else if (uReason == DLL_PROCESS_DETACH && g_hInstanceGuard)
	{
		CloseHandle(g_hInstanceGuard);
		g_hInstanceGuard = nullptr;
	}
	return TRUE;
}
