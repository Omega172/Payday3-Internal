#include "pch.h"
#include "AimbotHooks.hpp"
#include "../../Features/Aimbot/Aimbot.hpp"

#define ACTOR_GETACTOREYESVIEWPOINT_INDEX 0xE2
#define APLAYERCONTROLLER_GETPLAYERVIEWPOINT_INDEX 0x106

static SDK::FVector g_vecOriginalLocation{};
static SDK::FRotator g_rotOriginalRotation{};

static Hooking::Hook<void(*)(SDK::APlayerController*, SDK::FVector*, SDK::FRotator*)> oGetPlayerViewPoint;

static void hkGetPlayerViewPoint(SDK::APlayerController* pPlayerController, SDK::FVector* pLocation, SDK::FRotator* pRotation)
{
    oGetPlayerViewPoint(pPlayerController, pLocation, pRotation);

    if (!pLocation || !pRotation)
        return;

    if (!pAimbot->ShouldOverrideView)
        return;

}

static Hooking::Hook<void(*)(SDK::AActor*, SDK::FVector*, SDK::FRotator*)> oGetActorEyesViewPoint;

// Check if actor is localplayer, and just think of this as localplayerviewpoint ?
static void hkGetActorEyesViewPoint(SDK::AActor* pActor,SDK::FVector* pLocation, SDK::FRotator* pRotation)
{
    oGetActorEyesViewPoint(pActor, pLocation, pRotation);

    if (!pLocation || !pRotation)
        return;

	if (!pAimbot->ShouldOverrideView)
		return;

	SDK::APlayerController* pPlayerController = SDK::UGameplayStatics::GetPlayerController(SDK::UWorld::GetWorld(), 0);
	if (!pPlayerController)
		return;

	if (pActor != pPlayerController->AcknowledgedPawn)
		return;
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

    SDK::AActor* pPawn = pPlayerController->AcknowledgedPawn; // cus it's pawn it only populates when the player is in a level

    if (!pPawn)
		return false;

    Hooking::HookBatch batch;

    if (!batch.Install(
        oGetPlayerViewPoint,
        hkGetPlayerViewPoint,
        pPlayerController,
        APLAYERCONTROLLER_GETPLAYERVIEWPOINT_INDEX))
    {
        Utils::LogHook(
            "APlayerController::GetPlayerViewPoint",
            oGetPlayerViewPoint.GetStatus()
        );

        return false;
    }

    if (!batch.Install(
        oGetActorEyesViewPoint,
        hkGetActorEyesViewPoint,
        pPawn,
        ACTOR_GETACTOREYESVIEWPOINT_INDEX))
    {
        Utils::LogHook(
            "AActor::GetActorEyesViewPoint",
            oGetActorEyesViewPoint.GetStatus()
        );

        return false;
    }

    batch.Commit();

    m_bInstalled = true;

    Utils::LogDebug("AimbotHooks initialized successfully");

    return true;
}

void AimbotHooks::Destroy()
{
	oGetActorEyesViewPoint.Remove();
	oGetPlayerViewPoint.Remove();

	m_bInstalled = false;
	Utils::LogDebug("AimbotHooks removed.");
}
