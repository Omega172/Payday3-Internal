#include <Windows.h>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <utility>
#include <stacktrace>
#include <MinHook.h>
#pragma comment(lib, "version.lib")

#include "config.hpp"

#include "Utils/Console.hpp"
#include "Utils/Logging.hpp"
#include "Utils/Dx12Hook.hpp"
#include "Menu.hpp"
#include "Dumper-7/SDK.hpp"
#include "Features/Features.hpp"
#include "Features/FNames.hpp"
#include "Features/Misc/ClientMove.hpp"

#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

#include "Utils/UEOffsets.hpp"

#undef min
#undef max

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

	std::string consoleTitle = "Payday3-Internal Debug Console " + std::string(CURRENT_VERSION);
	Globals::g_upConsole = std::make_unique<Console>(Globals::g_bDebug, consoleTitle.c_str());
	if (!Globals::g_upConsole)
		return false;

	Utils::LogDebug("Attempting to acquire offsets this may take a moment...");
	std::chrono::time_point offsetStartTime = std::chrono::high_resolution_clock::now();

	std::optional<UEOffsets::Offsets> result = UEOffsets::Scan();
	if (!result) {
		Globals::g_upConsole->SetVisibility(true);
		Utils::LogError("Offset auto updating failed! Using fallback offsets. The internal may not work correctly or may even crash!");
		std::this_thread::sleep_for(std::chrono::seconds(3));
	} else {
		const UEOffsets::Offsets& offsets = result.value();
		SDK::Offsets::GObjects = offsets.GObjects;
		SDK::Offsets::AppendString = offsets.AppendString;
		SDK::Offsets::GNames = offsets.GNames;
		SDK::Offsets::ProcessEvent = offsets.ProcessEvent;
		SDK::Offsets::ProcessEventIdx = offsets.ProcessEventIdx;
		SDK::Offsets::GWorld = offsets.GWorld;

		Utils::LogDebug("Offsets updated successfully:");
		Utils::LogDebug(std::format("GObjects: 0x{:08X}", offsets.GObjects));
		Utils::LogDebug(std::format("AppendString: 0x{:08X}", offsets.AppendString));
		Utils::LogDebug(std::format("GNames: 0x{:08X}", offsets.GNames));
		Utils::LogDebug(std::format("GWorld: 0x{:08X}", offsets.GWorld));
		if (offsets.GWorld == 0)
			Utils::LogDebug("GWorld scan returned 0; using UEngine GameViewport fallback");
		Utils::LogDebug(std::format("ProcessEvent: 0x{:08X}", offsets.ProcessEvent));
		Utils::LogDebug(std::format("ProcessEventIdx: 0x{:08X}", offsets.ProcessEventIdx));
	}
	std::chrono::time_point offsetEndTime = std::chrono::high_resolution_clock::now();

	// Offset acquisition took 00:00:00:00 hh/mm/ss/ms
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(offsetEndTime - offsetStartTime);
	auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
	duration -= hours;
	auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
	duration -= minutes;
	auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
	duration -= seconds;
	auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
	Utils::LogDebug(std::format("Offset acquisition took {:02}:{:02}:{:02}:{:03} hh/mm/ss/ms", hours.count(), minutes.count(), seconds.count(), milliseconds.count()));

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
			SDK::Offsets::GWorld = result.value().GWorld;
			pGWorld = SDK::UWorld::GetWorld();
			if (!pGWorld) {
				Globals::g_upConsole->SetVisibility(true);
				Utils::LogError("Timeout while waiting for GWorld pointer!");
				return false;
			}
		}
	}

	int32_t iFramerateLimit = SDK::USBZSettingsFunctionsVideo::GetFramerateLimit(pGWorld);
	Utils::LogDebug(std::format("GWorld pointer acquired: 0x{:016X}", reinterpret_cast<uint64_t>(pGWorld)));

	Utils::LogDebug("[init-stage] Before MH_Initialize");
	if (MH_Initialize() != MH_OK) {
		Globals::g_upConsole->SetVisibility(true);
		Utils::LogError("Failed to initialize MinHook library!");
		return false;
	}
	Utils::LogDebug("[init-stage] After MH_Initialize, before Dx12Hook::Initialize");

    if (!Dx12Hook::Initialize()) {
        Globals::g_upConsole->SetVisibility(true);
        Utils::LogError("Failed to initialize DirectX 12 hook!");
        return false;
    }
    Utils::LogDebug("[init-stage] After Dx12Hook::Initialize");

	SDK::USBZSettingsFunctionsVideo::SetFramerateLimit(pGWorld, iFramerateLimit);
	return true;
}

#include "Dumper-7/SDK/Engine_parameters.hpp"
#include "Dumper-7/SDK/Starbreeze_parameters.hpp"
#include "Dumper-7/SDK/GameplayAbilities_parameters.hpp"

SDK::ASBZPlayerCharacter* GetLocalPlayer()
{
	SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
	if (!pGWorld)
		return{};

	SDK::UGameInstance* pGameInstance = pGWorld->OwningGameInstance;
	if (!pGameInstance)
		return{};

	if (!pGameInstance->LocalPlayers.IsValidIndex(0))
		return{};

	SDK::ULocalPlayer* pLocalPlayer = pGameInstance->LocalPlayers[0];
	if (!pLocalPlayer)
		return{};

	SDK::ASBZPlayerController* pLocalPlayerController = reinterpret_cast<SDK::ASBZPlayerController*>(pLocalPlayer->PlayerController);
	if (!pLocalPlayerController || !pLocalPlayerController->IsA(SDK::ASBZPlayerController::StaticClass()))
		return{};

	SDK::ASBZPlayerCharacter* pLocalPlayerPawn = reinterpret_cast<SDK::ASBZPlayerCharacter*>(pLocalPlayerController->AcknowledgedPawn);
	if (!pLocalPlayerPawn || !pLocalPlayerPawn->IsA(SDK::ASBZPlayerCharacter::StaticClass()))
		return{};

	return pLocalPlayerPawn;
}

inline void RecordProcessEventCall(const SDK::UObject* pObject, class SDK::UFunction* pFunction, void* pParams){
	auto sClassName = pObject->Class->Name.GetRawString();
	auto sFnName = pFunction->GetName();
	size_t iNameHash = std::hash<std::string>{}(sClassName);
	size_t iFuncHash = std::hash<std::string>{}(sFnName);

	if (auto itrEntry = Menu::g_mapCallTraces.find(iNameHash); itrEntry != Menu::g_mapCallTraces.end())
		itrEntry->second.m_mapCalledFunctions.try_emplace(iFuncHash, sFnName);
	else {
		Menu::CallTraceEntry_t entry{
			.m_sClassName = sClassName
		};

		entry.m_mapCalledFunctions.try_emplace(iFuncHash, sFnName);

		auto pStruct = static_cast<const SDK::UStruct*>(pObject->Class);
		for (pStruct = static_cast<const SDK::UStruct*>(pStruct->SuperStruct); pStruct != nullptr; pStruct = static_cast<const SDK::UStruct*>(pStruct->SuperStruct))
			entry.m_vecSubClasses.push_back(pStruct->Name.GetRawString());
		
		Menu::g_mapCallTraces.try_emplace(iNameHash, std::move(entry));
	}
}

bool g_bAttemptedToShoot = false;

using UObjectProcessEvent_t = void(*)(const SDK::UObject*, class SDK::UFunction*, void*);
UObjectProcessEvent_t UObjectProcessEvent_o = nullptr;
void UObjectProcessEvent_hk(const SDK::UObject* pObject, class SDK::UFunction* pFunction, void* pParams)
{
	if (!pObject || !pObject->Class || !pFunction) {
		UObjectProcessEvent_o(pObject, pFunction, pParams);
		return;
	}

	auto nameObject = pObject->Name;
	auto nameClass = pObject->Class->Name;
	auto nameSuper = pObject->Class->SuperStruct ? pObject->Class->SuperStruct->Name : SDK::FName{};
	auto nameFunction = pFunction->Name;

	if(!Cheat::g_bIsInGame || pObject->Name == FNames::KismetStringLibrary){
		UObjectProcessEvent_o(pObject, pFunction, pParams);
		return;
	}

	if(Menu::g_eCallTraceArea == Menu::ECallTraceArea::UObject)
		RecordProcessEventCall(pObject, pFunction, pParams);

	if(nameFunction == FNames::ClientPlayForceFeedback_Internal)
		return;

	if(nameFunction == FNames::ServerTryActivateAbility){
		UObjectProcessEvent_o(pObject, pFunction, pParams);
		auto& params = *reinterpret_cast<SDK::Params::AbilitySystemComponent_ServerTryActivateAbility*>(pParams);	
		return;
	}

	if(nameClass == FNames::SBZCookingStation || nameSuper == FNames::SBZCookingStation)
		Cheat::g_iMethLabIndex = pObject->Index;
		
	if(nameSuper == FNames::SBZKeypad){
		auto pKeypad = reinterpret_cast<SDK::ASBZKeypadBase*>(const_cast<SDK::UObject*>(pObject));
		
		uint8_t iActiveKey = 0;
		switch(pKeypad->Inputs)
		{
		case(0):
			iActiveKey = pKeypad->Code / 1000;
			break;

		case(1):	
			iActiveKey = (pKeypad->Code / 100) % 10;
			break;
			
		case(2):
			iActiveKey = (pKeypad->Code / 10) % 10;
			break;

		case(3):
			iActiveKey = pKeypad->Code % 10;
			break;

		default:
			iActiveKey = (pKeypad->GuessedCode != pKeypad->Code) ? 10 : 11;
			break;
		}

		for (int i = 0; i < pKeypad->KeypadInteractableComponentArray.Num(); i++)
		{
			auto pKey = pKeypad->KeypadInteractableComponentArray[i];
			if(!pKey)
				continue;
			
			pKey->SetLocalEnabled(i == static_cast<int>(iActiveKey));
		}

		UObjectProcessEvent_o(pObject, pFunction, pParams);
		return;
	}
	
	if(!pObject->Class || !pObject->Class->SuperStruct){
		UObjectProcessEvent_o(pObject, pFunction, pParams);
		return;
	}

	if(nameFunction == FNames::ServerMovePacked){
		//std::cout << nameSuper.ToString() << '\n';
	}

	if(nameSuper == FNames::CH_PlayerBase_C && nameFunction == FNames::ServerMovePacked){
		if(Cheat::ClientMove::ShouldSendMovePacket())
			UObjectProcessEvent_o(pObject, pFunction, pParams);
		
		return;
	}

	if(nameSuper == FNames::CH_PlayerBase_C && nameFunction == FNames::ClientMoveResponsePacked){
		
		//Cheat::g_vecLastUpdatedLocation = GetLocalPlayer()->K2_GetActorLocation();
		UObjectProcessEvent_o(pObject, pFunction, pParams);
		return;
	}

	if(nameSuper == FNames::SBZActionInputWidget){
		auto pWidget = reinterpret_cast<const SDK::USBZActionInputWidget*>(pObject);
		if(nameFunction == FNames::OnActionPressed && pWidget->ActionName == FNames::MaskOn){
			//std::cout << "ActionPressed: " << pWidget->ActionName.ToString() << '\n';
			if(!Cheat::g_bIsInStealth)
				g_bAttemptedToShoot = true;
			
			auto pLocalPlayer = GetLocalPlayer();
			if(pLocalPlayer && pLocalPlayer->SBZPlayerState && pLocalPlayer->PlayerAbilitySystem && CheatConfig::Get().m_misc.m_bInstantInteraction){
				if(!pLocalPlayer->SBZPlayerState->bIsMaskOn)
					pLocalPlayer->PlayerAbilitySystem->Server_MaskOn();
			}
		}
		else if(nameFunction == FNames::OnActionReleased){
			//std::cout << "ActionReleased: " << pWidget->ActionName.ToString() << '\n';
		}
	}

	UObjectProcessEvent_o(pObject, pFunction, pParams);
}

UObjectProcessEvent_t UObjectProcessEventPlayer_o = nullptr;
void UObjectProcessEventPlayer_hk(const SDK::UObject* pObject, class SDK::UFunction* pFunction, void* pParams)
{
	if(!Cheat::g_bIsInGame || !pObject->Class || !pObject->Class->SuperStruct){
		UObjectProcessEventPlayer_o(pObject, pFunction, pParams);
		return;
	}

	if(Menu::g_eCallTraceArea == Menu::ECallTraceArea::PlayerController)
		RecordProcessEventCall(pObject, pFunction, pParams);

	if(pObject->Class->SuperStruct->Name == FNames::CH_PlayerBase_C){
		if(pFunction->Name == FNames::ReceiveTick)
			Cheat::OnPlayerControllerTick();
		else if(pFunction->Name == FNames::Server_ThrowBag){
			auto pPlayer = reinterpret_cast<const SDK::ACH_PlayerBase_C*>(pObject);

			if(CheatConfig::Get().m_misc.m_bSuperToss && pPlayer->Controller && pPlayer->Controller->IsA(SDK::APlayerController::StaticClass())){
				auto pController = reinterpret_cast<SDK::APlayerController*>(pPlayer->Controller);
				if(pController->PlayerCameraManager){
					auto& params = *reinterpret_cast<SDK::Params::SBZCharacter_Server_ThrowBag*>(pParams);
					params.ReplicatedVelocity = SDK::UKismetMathLibrary::GetForwardVector(pController->PlayerCameraManager->GetCameraRotation()) * CheatConfig::Get().m_misc.m_flSuperToss;
				}
			}
		}
	}
		
	UObjectProcessEventPlayer_o(pObject, pFunction, pParams);
}

static SDK::FVector g_vecOriginalLocation{};
static SDK::FRotator g_rotOriginalRotation{};

using ULocalPlayerGetViewPoint_t = void(*)(SDK::ULocalPlayer*, SDK::FMinimalViewInfo*);
ULocalPlayerGetViewPoint_t ULocalPlayerGetViewPoint_o = nullptr;
void ULocalPlayerGetViewPoint_hk(SDK::ULocalPlayer* _this, SDK::FMinimalViewInfo* OutViewInfo)
{
	ULocalPlayerGetViewPoint_o(_this, OutViewInfo);
	if(!Cheat::g_bIsInGame)
		return;

	OutViewInfo->Location = g_vecOriginalLocation;
	OutViewInfo->Rotation = g_rotOriginalRotation;	
}

using APlayerControllerGetPlayerViewPoint_t = void(*)(SDK::APlayerController*, SDK::FVector*, SDK::FRotator*);
APlayerControllerGetPlayerViewPoint_t APlayerControllerGetPlayerViewPoint_o = nullptr;
void APlayerControllerGetPlayerViewPoint_hk(SDK::APlayerController* _this, SDK::FVector* out_Location, SDK::FRotator* out_Rotation)
{
	static std::vector<std::pair<uintptr_t, uint64_t>> vecHackyShitVector{};
	static uintptr_t pGoalRet1 = 0;
	static uintptr_t pGoalRet2 = 0;
	APlayerControllerGetPlayerViewPoint_o(_this, out_Location, out_Rotation);
	if(!Cheat::g_bIsInGame){
		vecHackyShitVector.clear();
		return;
	}
		
	g_vecOriginalLocation = *out_Location;
	g_rotOriginalRotation = *out_Rotation;

	auto pReturnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	if(!pGoalRet1 || !pGoalRet2){
		bool bFound = false;
		for(size_t i = 0; i < vecHackyShitVector.size(); ++i){
			if(vecHackyShitVector[i].first == pReturnAddress){
				vecHackyShitVector[i].second++;
				bFound = true;
				break;
			}
		}

		if(!bFound)
			vecHackyShitVector.emplace_back(std::pair<uintptr_t, uint64_t>{ pReturnAddress, 1 });

		if(g_bAttemptedToShoot){
			if(vecHackyShitVector.size() > 5){
				std::pair<uintptr_t, uint64_t> pairs[3]{};

				for(size_t i = 0; i < vecHackyShitVector.size(); ++i){
					if(vecHackyShitVector[i].second < pairs[0].second || !pairs[0].first){
						pairs[2] = pairs[1];
						pairs[1] = pairs[0];
						pairs[0] = vecHackyShitVector[i];
						continue;
					}

					if(vecHackyShitVector[i].second < pairs[1].second || !pairs[1].first){
						pairs[2] = pairs[1];
						pairs[1] = vecHackyShitVector[i];
						continue;
					}

					if(vecHackyShitVector[i].second < pairs[2].second || !pairs[2].first){
						pairs[2] = vecHackyShitVector[i];
						continue;
					}
				}

				if(pairs[2].second > 6000 && pairs[1].second - pairs[0].second < pairs[2].second - pairs[1].second){
					pGoalRet1 = pairs[0].first;
					pGoalRet2 = pairs[1].first;
					vecHackyShitVector.clear();

					std::cout << "pAimbotFixup\n";
				}
			}
		}

		g_bAttemptedToShoot = false;
	}
	else if(pGoalRet1 != pReturnAddress && pGoalRet2 != pReturnAddress)
		return;
	
	if(!Cheat::g_stTargetInfo || !CheatConfig::Get().m_aimbot.m_bEnabled || (Cheat::g_bIsInStealth && CheatConfig::Get().m_aimbot.m_bDisableInStealth))
		return;

	SDK::FRotator rotCurrent = *out_Rotation;
	SDK::FRotator rotGoal = Cheat::g_stTargetInfo->m_rotAimRotation;
	//*out_Rotation = (rotGoal - ((rotGoal - rotCurrent).GetNormalized() * 0.5f)).GetNormalized();
	*out_Rotation = rotGoal;
	*out_Location = Cheat::g_stTargetInfo->m_vecAimPosition;
}

inline void HookFunction(const std::string& sName, void* pFunction, void* pDetour, void** ppOriginal, std::source_location loc = std::source_location::current()){
	auto eStatus = MH_CreateHook(pFunction, pDetour, ppOriginal);
	Utils::LogHook(sName, eStatus, loc);
	if(eStatus != MH_OK)
		return;

	Utils::LogHook(sName, MH_EnableHook(pFunction), loc);
}

void MainLoop()
{	
	#define ContinueLoop {std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue;}
	while (!GetAsyncKeyState(UNLOAD_KEY) && !GetAsyncKeyState(UNLOAD_KEY_ALT))
    {
		if (GetAsyncKeyState(CONSOLE_KEY) & 0x1)
			Globals::g_upConsole->ToggleVisibility();

		SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
		if (!pGWorld)
			ContinueLoop

		SDK::UGameInstance* pGameInstance = pGWorld->OwningGameInstance;
		if (!pGameInstance)
			ContinueLoop

		if (!pGameInstance->LocalPlayers.IsValidIndex(0))
			ContinueLoop

		SDK::ULocalPlayer* pLocalPlayer = pGameInstance->LocalPlayers[0];
		if (!pLocalPlayer)
			ContinueLoop

		SDK::APlayerController* pLocalPlayerControllerBase = pLocalPlayer->PlayerController;
		if (!pLocalPlayerControllerBase)
			ContinueLoop

		SDK::ASBZPlayerController* pLocalPlayerController = reinterpret_cast<SDK::ASBZPlayerController*>(pLocalPlayerControllerBase);
		if (!pLocalPlayerController || !pLocalPlayerController->IsA(SDK::ASBZPlayerController::StaticClass())){
			Cheat::g_bIsInGame = false;
			ContinueLoop
		}

		auto pGameState = reinterpret_cast<SDK::APD3HeistGameState*>(pGWorld->GameState);
		if(!pGameState || !pGameState->IsA(SDK::APD3HeistGameState::StaticClass())){
			Cheat::g_bIsInGame = false;
			ContinueLoop
		}

		auto pGameStateMachine = SDK::USBZGameStateMachineFunctionLibrary::GetGameStateMachine(pGWorld);
		auto eMachineState = SDK::USBZGameStateMachineFunctionLibrary::GetGameStateMachineState(pGWorld);
		if(!pGameStateMachine){
			Cheat::g_bIsInGame = false;
			ContinueLoop
		}

		const bool bInActionPhase = eMachineState == SDK::ESBZGameStateMachineState::SM_ActionPhase;
		Cheat::g_bIsInStealth = pGameState->CurrentHeistState == SDK::EPD3HeistState::Stealth || pGameState->CurrentHeistState == SDK::EPD3HeistState::Search;
		Cheat::g_bIsInGame = bInActionPhase;

		if (!bInActionPhase)
			ContinueLoop

		FNames::Initialize();

		if (!UObjectProcessEvent_o && !FNames::KismetStringLibrary.IsNone())
			HookFunction("UObjectProcessEvent", SDK::InSDKUtils::GetVirtualFunction<void*>(pGWorld, SDK::Offsets::ProcessEventIdx), reinterpret_cast<void*>(&UObjectProcessEvent_hk), reinterpret_cast<void**>(&UObjectProcessEvent_o));

		if (!ULocalPlayerGetViewPoint_o)
			HookFunction("ULocalPlayerGetViewPoint", SDK::InSDKUtils::GetVirtualFunction<void*>(pLocalPlayer, ULOCALPLAYER_GETVIEWPOINT_INDEX), reinterpret_cast<void*>(&ULocalPlayerGetViewPoint_hk), reinterpret_cast<void**>(&ULocalPlayerGetViewPoint_o));

		if (!APlayerControllerGetPlayerViewPoint_o)
			HookFunction("APlayerControllerGetPlayerViewPoint", SDK::InSDKUtils::GetVirtualFunction<void*>(pLocalPlayerControllerBase, APLAYERCONTROLLER_GETPLAYERVIEWPOINT_INDEX), reinterpret_cast<void*>(&APlayerControllerGetPlayerViewPoint_hk), reinterpret_cast<void**>(&APlayerControllerGetPlayerViewPoint_o));

		if(!UObjectProcessEventPlayer_o)
			HookFunction("UObjectProcessEventPlayer", SDK::InSDKUtils::GetVirtualFunction<void*>(pLocalPlayerControllerBase, SDK::Offsets::ProcessEventIdx), reinterpret_cast<void*>(&UObjectProcessEventPlayer_hk), reinterpret_cast<void**>(&UObjectProcessEventPlayer_o));

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	#undef ContinueLoop
}

bool VerifyGameVersion()
{
	char szBlock[MAX_PATH];
	GetModuleFileNameA(Globals::g_hBaseModule, szBlock, MAX_PATH);
	Utils::LogDebug(std::string("Module Name: ") + szBlock);

	Globals::g_upConsole->SetVisibility(true);

	// Get current module file version and compare with target version
	std::vector<BYTE> vecVersionData(GetFileVersionInfoSizeA(szBlock, NULL));
	if(!vecVersionData.size() || !GetFileVersionInfoA(szBlock, 0, vecVersionData.size(), vecVersionData.data())){
		Utils::LogError("Failed to open game to find version info!");
		return false;
	}

	std::pair<WORD, WORD>* lpTranslate{};
	UINT iTranslations{};
	if(!VerQueryValueA(vecVersionData.data(), "\\VarFileInfo\\Translation", reinterpret_cast<void**>(&lpTranslate), &iTranslations) || !lpTranslate || !iTranslations){
		Utils::LogError("Failed to query for game translations!");
		return false;
	}

	memset(szBlock, 0, sizeof(szBlock));
	snprintf(szBlock, sizeof(szBlock), "\\StringFileInfo\\%04x%04x\\ProductVersion", lpTranslate[0].first, lpTranslate[0].second);

	void* lpBuffer{};
	UINT iBytes{};

	if(!VerQueryValueA(vecVersionData.data(), szBlock, &lpBuffer, &iBytes) || !iBytes || !lpBuffer){
		Utils::LogError("Failed to find game version!");
		return false;
	}

	if(std::string(static_cast<char*>(lpBuffer)) != TARGET_VERSION) {
		Utils::LogError(std::string("Game version mismatch! Expected ") + TARGET_VERSION + ", got " + static_cast<char*>(lpBuffer));
	}

	return true;
}

DWORD WINAPI MainThread(LPVOID lpParam)
{
	bool bInitSuccess = Init();
	bool bOriginalVisibility = Globals::g_upConsole->GetVisibility();
	bInitSuccess &= VerifyGameVersion();

	if (!CheatConfig::Get().Load())
		Utils::LogDebug("Config load skipped or failed, using in-memory defaults.");

	(bInitSuccess) ? Utils::LogDebug("Initialization successful") : Utils::LogError("Initialization failed!");

	std::this_thread::sleep_for(std::chrono::seconds(5));
	Globals::g_upConsole->SetVisibility(bOriginalVisibility);

	if (bInitSuccess)
		MainLoop();
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
