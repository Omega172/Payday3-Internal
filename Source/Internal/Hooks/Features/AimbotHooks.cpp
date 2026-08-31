#include "pch.h"
#include "AimbotHooks.hpp"
#include "../../Features/Aimbot/Aimbot.hpp"

#pragma intrinsic(_ReturnAddress)

#define ACTOR_GETACTOREYESVIEWPOINT_INDEX 0xE2
#define APLAYERCONTROLLER_GETPLAYERVIEWPOINT_INDEX 0x106

using GetPlayerViewPointFn = void(*)(SDK::APlayerController*, SDK::FVector*, SDK::FRotator*);
using GetActorEyesViewPointFn = void(*)(SDK::AActor*, SDK::FVector*, SDK::FRotator*);

static Hooking::Hook<GetPlayerViewPointFn> oGetPlayerViewPoint;
static Hooking::Hook<GetActorEyesViewPointFn> oGetActorEyesViewPoint;

static std::vector<std::pair<uintptr_t, uint64_t>> g_vecRetCounts{};
static uintptr_t g_pFireRet1 = 0;
static uintptr_t g_pFireRet2 = 0;
static bool g_bSilentReady = false;
static uint64_t g_ullOverridesApplied = 0;

static void ResetSilentCalibration()
{
	g_vecRetCounts.clear();
	g_pFireRet1 = 0;
	g_pFireRet2 = 0;
	g_bSilentReady = false;
	g_ullOverridesApplied = 0;
}

static void LearnFirePath(uintptr_t pReturnAddress)
{
	if (g_bSilentReady)
		return;

	bool bFound = false;
	for (auto& entry : g_vecRetCounts)
	{
		if (entry.first == pReturnAddress)
		{
			entry.second++;
			bFound = true;
			break;
		}
	}
	if (!bFound)
		g_vecRetCounts.emplace_back(pReturnAddress, 1ull);

	if (g_vecRetCounts.size() <= 5)
		return;

	std::pair<uintptr_t, uint64_t> pairs[3]{};
	for (const auto& entry : g_vecRetCounts)
	{
		if (entry.second < pairs[0].second || !pairs[0].first)
		{
			pairs[2] = pairs[1];
			pairs[1] = pairs[0];
			pairs[0] = entry;
			continue;
		}
		if (entry.second < pairs[1].second || !pairs[1].first)
		{
			pairs[2] = pairs[1];
			pairs[1] = entry;
			continue;
		}
		if (entry.second < pairs[2].second || !pairs[2].first)
			pairs[2] = entry;
	}

	// Same shape as custom; slightly lower floor so it locks sooner in PD3.
	if (pairs[2].second > 2000
		&& pairs[1].second >= pairs[0].second
		&& (pairs[1].second - pairs[0].second) < (pairs[2].second - pairs[1].second))
	{
		g_pFireRet1 = pairs[0].first;
		g_pFireRet2 = pairs[1].first;
		g_bSilentReady = true;
		g_vecRetCounts.clear();
		Utils::LogDebug(std::format(
			"pSilent READY — firePath 0x{:X} / 0x{:X}",
			g_pFireRet1, g_pFireRet2));
	}
}

static void hkGetPlayerViewPoint(SDK::APlayerController* pPlayerController, SDK::FVector* pLocation, SDK::FRotator* pRotation)
{
	oGetPlayerViewPoint(pPlayerController, pLocation, pRotation);

	if (!pLocation || !pRotation)
		return;

	const uintptr_t pReturnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	LearnFirePath(pReturnAddress);

	if (!pAimbot || !pAimbot->ShouldOverrideView)
		return;

	if (pAimbot->CurrentAimbotType() != 0)
		return;

	// CUSTOM BEHAVIOR:
	//  - Before READY: apply on every call (bullets work; camera may twitch while learning)
	//  - After READY: apply ONLY on fire-path returns (true client silent / pSilent)
	if (g_bSilentReady)
	{
		if (pReturnAddress != g_pFireRet1 && pReturnAddress != g_pFireRet2)
			return;
	}

	*pLocation = pAimbot->OverrideLocation;
	*pRotation = pAimbot->OverrideRotation;
	++g_ullOverridesApplied;
}

static void hkGetActorEyesViewPoint(SDK::AActor* pActor, SDK::FVector* pLocation, SDK::FRotator* pRotation)
{
	oGetActorEyesViewPoint(pActor, pLocation, pRotation);
	(void)pActor;
	(void)pLocation;
	(void)pRotation;
}

bool AimbotHooks::Setup()
{
	if (m_bInstalled)
		return true;

	SDK::UWorld* pWorld = SDK::UWorld::GetWorld();
	if (!pWorld)
		return false;

	SDK::APlayerController* pPlayerController = SDK::UGameplayStatics::GetPlayerController(pWorld, 0);
	if (!pPlayerController)
		return false;

	SDK::AActor* pPawn = pPlayerController->AcknowledgedPawn;
	if (!pPawn)
		return false;

	if (!Memory::GetVirtualMethod(pPlayerController, APLAYERCONTROLLER_GETPLAYERVIEWPOINT_INDEX)
		|| !Memory::GetVirtualMethod(pPawn, ACTOR_GETACTOREYESVIEWPOINT_INDEX))
	{
		Utils::LogError("AimbotHooks: viewpoint vtable slot null");
		return false;
	}

	Hooking::HookBatch batch;

	if (!batch.Install(
		oGetPlayerViewPoint,
		hkGetPlayerViewPoint,
		pPlayerController,
		APLAYERCONTROLLER_GETPLAYERVIEWPOINT_INDEX))
	{
		Utils::LogHook("APlayerController::GetPlayerViewPoint", oGetPlayerViewPoint.GetStatus());
		return false;
	}

	if (!batch.Install(
		oGetActorEyesViewPoint,
		hkGetActorEyesViewPoint,
		pPawn,
		ACTOR_GETACTOREYESVIEWPOINT_INDEX))
	{
		Utils::LogHook("AActor::GetActorEyesViewPoint", oGetActorEyesViewPoint.GetStatus());
		return false;
	}

	batch.Commit();
	ResetSilentCalibration();
	m_bInstalled = true;

	Utils::LogDebug("AimbotHooks ready (pSilent: learn-then-gate @ 0x106)");
	return true;
}

void AimbotHooks::Destroy()
{
	oGetActorEyesViewPoint.Remove();
	oGetPlayerViewPoint.Remove();
	ResetSilentCalibration();
	m_bInstalled = false;
	Utils::LogDebug("AimbotHooks removed.");
}
