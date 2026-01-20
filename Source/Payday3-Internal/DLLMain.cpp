#include <Windows.h>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <stacktrace>
#include <MinHook.h>
#pragma comment(lib, "version.lib")

#include "config.hpp"

import Utils.Console;
import Utils.Logging;
import Hook.Dx12Hook;

#include "Dumper-7/SDK.hpp"

namespace Globals {
#ifdef _DEBUG
	static constexpr bool g_bDebug = true;
#else
	static constexpr bool g_bDebug = false;
#endif
	
	static HMODULE g_hModule;
	static HMODULE g_hBaseModule;
	std::unique_ptr<Console> g_upConsole;
}

bool Init() 
{
	Globals::g_hBaseModule = GetModuleHandleA(NULL);

	Globals::g_upConsole = std::make_unique<Console>(Globals::g_bDebug, "Payday3-Internal Debug Console");
	if (!Globals::g_upConsole)
		return false;

	SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
	if (!pGWorld) {
		bool bOriginalVisibility = Globals::g_upConsole->GetVisibility();
		Globals::g_upConsole->SetVisibility(true);

		Utils::LogError("Failed to get GWorld pointer!\n Will wait for 30 seconds before aborting...");
		std::this_thread::sleep_for(std::chrono::seconds(3));
		Globals::g_upConsole->SetVisibility(bOriginalVisibility);
	}

	std::chrono::time_point startTime = std::chrono::high_resolution_clock::now();
	while (!pGWorld) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		pGWorld = SDK::UWorld::GetWorld();

		std::chrono::time_point currentTime = std::chrono::high_resolution_clock::now();
		auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();
		if (elapsedTime >= 30) {
			Globals::g_upConsole->SetVisibility(true);
			Utils::LogError("Timeout while waiting for GWorld pointer!");
			return false;
		}
	}

	Utils::LogDebug("GWorld pointer acquired: " + std::to_string(reinterpret_cast<uint64_t>(pGWorld)));

	if (MH_Initialize() != MH_OK) {
		Globals::g_upConsole->SetVisibility(true);
		Utils::LogError("Failed to initialize MinHook library!");
		return false;
	}

    if (!Dx12Hook::Initialize()) {
        Globals::g_upConsole->SetVisibility(true);
        Utils::LogError("Failed to initialize DirectX 12 hook!");
        return false;
    }

	return true;
}

DWORD WINAPI MainThread(LPVOID lpParam)
{
	bool bInitSuccess = Init();
	(bInitSuccess) ? Utils::LogDebug("Initialization successful") : Utils::LogError("Initialization failed!");

    while (bInitSuccess && !GetAsyncKeyState(UNLOAD_KEY))
    {
		if (GetAsyncKeyState(CONSOLE_KEY) & 0x1)
			Globals::g_upConsole->ToggleVisibility();

		static std::once_flag mainLoopInit;
		std::call_once(mainLoopInit, [&bInitSuccess]() {
			// Get current module name
			char moduleName[MAX_PATH];
			GetModuleFileNameA(Globals::g_hBaseModule, moduleName, MAX_PATH);
			Utils::LogDebug(std::string("Module Name: ") + moduleName);

			boolean bOriginalVisibility = Globals::g_upConsole->GetVisibility();
			Globals::g_upConsole->SetVisibility(true);

			// Get current module file version and compare with target version
			DWORD versionInfoSize = GetFileVersionInfoSizeA(moduleName, NULL);
			if (versionInfoSize > 0) {
				std::vector<BYTE> versionData(versionInfoSize);
				if (GetFileVersionInfoA(moduleName, 0, versionInfoSize, versionData.data())) {
					// First get the translation information to determine the language and code page
					struct LANGANDCODEPAGE {
						WORD wLanguage;
						WORD wCodePage;
					} *lpTranslate;
					
					UINT cbTranslate;
					if (VerQueryValueA(versionData.data(), "\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate)) {
						// Build the path to the ProductVersion string
						char subBlock[256];
						snprintf(subBlock, sizeof(subBlock), "\\StringFileInfo\\%04x%04x\\ProductVersion", 
							lpTranslate[0].wLanguage, lpTranslate[0].wCodePage);
						
						// Query the ProductVersion string
						LPVOID lpBuffer;
						UINT dwBytes;
						if (VerQueryValueA(versionData.data(), subBlock, &lpBuffer, &dwBytes) && dwBytes > 0) {
							std::string productVersion = static_cast<char*>(lpBuffer);
							Utils::LogDebug(std::string("Product Version: ") + productVersion);
							Utils::LogDebug(std::string("Target Version: ") + TARGET_VERSION);
							
							if (productVersion == TARGET_VERSION) {
								Utils::LogDebug("Version match: Target version found!");
							} else {
								Utils::LogDebug("Version mismatch: Target version not found!");
								bInitSuccess = false;
							}
						} else {
							Utils::LogError("Failed to query ProductVersion string");
							bInitSuccess = false;
						}
					} else {
						Utils::LogError("Failed to query translation information");
						bInitSuccess = false;
					}
				} else {
					Utils::LogError("Failed to get file version info");
					bInitSuccess = false;
				}
			} else {
				Utils::LogDebug("No version information available for this module");
				bInitSuccess = false;
			}

			std::this_thread::sleep_for(std::chrono::seconds(5));
			Globals::g_upConsole->SetVisibility(bOriginalVisibility);
		});

		// Do frame independent work here
		SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
		if (!pGWorld)
			continue;

		SDK::UGameInstance* pGameInstance = pGWorld->OwningGameInstance;
		if (!pGameInstance)
			continue;

		SDK::ULocalPlayer* pLocalPlayer = pGameInstance->LocalPlayers[0];
		if (!pLocalPlayer)
			continue;

		SDK::APawn* pAcknowledgedPawn = pLocalPlayer->PlayerController->AcknowledgedPawn;
		if (!pAcknowledgedPawn)
			continue;

		if (pAcknowledgedPawn->IsA(SDK::ASBZPlayerCharacter::StaticClass())) {
			SDK::ASBZPlayerCharacter* pLocalPlayerCharacter = static_cast<SDK::ASBZPlayerCharacter*>(pAcknowledgedPawn);
			if (!pLocalPlayerCharacter)
				continue;

			pLocalPlayerCharacter->CarryTiltDegrees = 0.0f;
			pLocalPlayerCharacter->CarryTiltSpeed = 0.0f;
			pLocalPlayerCharacter->CarryAdditionalTiltDegrees = 0.0f;
		}

		SDK::APlayerController* pLocalPlayerController = pLocalPlayer->PlayerController;
		if (!pLocalPlayerController)
			continue;

		if (pLocalPlayerController->IsA(SDK::ASBZPlayerController::StaticClass())) {
			SDK::ASBZPlayerController* pSBZPlayerController = static_cast<SDK::ASBZPlayerController*>(pLocalPlayerController);
			if (!pSBZPlayerController)
				continue;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	Utils::LogDebug("Unloading...");

	// Shutdown DirectX 12 hook
	if (Dx12Hook::IsInitialized())
	{
		Utils::LogDebug("Shutting down DirectX 12 hook...");
		Dx12Hook::Shutdown();
	}

	// Uninitialize MinHook
	MH_STATUS mhStatus = MH_Uninitialize();
	if (mhStatus == MH_OK)
		Utils::LogDebug("MinHook uninitialized successfully");
	else
		Utils::LogHook("MinHook Uninitialize", mhStatus);

	// Give time for cleanup to complete
	std::this_thread::sleep_for(std::chrono::seconds(3));
	
	// Destroy console last
	Globals::g_upConsole.get()->Destroy();

	std::this_thread::sleep_for(std::chrono::seconds(1));
    FreeLibraryAndExitThread(Globals::g_hModule, 0);
    return TRUE;
}

LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
	std::stringstream ssStackTrace;
	ssStackTrace << std::endl;
	auto stackTrace = std::stacktrace::current();
	for (const auto& frame : stackTrace) {
		ssStackTrace << frame << std::endl;
	}
	Utils::LogError(ssStackTrace.str());

	if (!Globals::g_bDebug)
		exit(EXIT_FAILURE);

	return EXCEPTION_EXECUTE_HANDLER;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall, LPVOID lpReserved)
{
	if (ulReasonForCall != DLL_PROCESS_ATTACH)
		return TRUE;

	Globals::g_hModule = hModule;

	SetUnhandledExceptionFilter(ExceptionHandler);
	DisableThreadLibraryCalls(hModule);
	HANDLE hThread = CreateThread(NULL, 0, MainThread, hModule, 0, NULL);
	if (hThread)
		CloseHandle(hThread);

	return TRUE;
}
