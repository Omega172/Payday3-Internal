#include "Features.hpp"

namespace Cheat{
    void GetOptimalAimbotTarget(SDK::UWorld* pGWorld, SDK::USBZWorldRuntime* pWorldRuntime, SDK::ASBZPlayerController* pPlayerController){
        static int32_t iLocalPlayerIndex = std::numeric_limits<int32_t>::lowest();
        if(pPlayerController && pPlayerController->AcknowledgedPawn)
            iLocalPlayerIndex = pPlayerController->AcknowledgedPawn->Index;

        static auto nameHead = SDK::UKismetStringLibrary::Conv_StringToName(L"Head");

        if(!g_bShouldBeAiming){
            g_stTargetInfo = {};
            return;
        }

        if(!pWorldRuntime->AllPawns || !pPlayerController->PlayerCameraManager)
            return;

        SDK::FVector vecCameraLocation = pPlayerController->PlayerCameraManager->GetCameraLocation();
        SDK::FRotator rotCameraRotation = pPlayerController->PlayerCameraManager->GetCameraRotation();

        const auto& stConfig = CheatConfig::Get().m_aimbot;

        auto GetTargetInfoForDrone = [&](SDK::ASBZAIDrone* pDrone) -> std::optional<TargetInfo_t> {
            return{};
        };

        //AITaserMine

        auto GetTargetInfo = [&](SDK::AActor* pActor) -> std::optional<TargetInfo_t> {
            if(pActor->IsA(SDK::ASBZAIDrone::StaticClass()))
                return GetTargetInfoForDrone(reinterpret_cast<SDK::ASBZAIDrone*>(pActor));
            else if(!pActor->IsA(SDK::ACH_BaseHumanAI_C::StaticClass()))
                return{};
            
            auto pHuman = reinterpret_cast<SDK::ACH_BaseHumanAI_C*>(pActor);
            if(!pHuman->bIsAlive || !pHuman->bCanBeDamaged || !pHuman->bIsDeathAllowed || pHuman->bActorEnableIgnoredCollision || !pHuman->bActorEnableCollision)
                return{};


            SDK::FVector vecTargetPosition = pHuman->Mesh->GetSocketLocation(nameHead);
            SDK::FVector vecAimPosition = vecTargetPosition;
            SDK::FRotator rotAimRotation = SDK::UKismetMathLibrary::FindLookAtRotation(SDK::FVector{}, pHuman->GetVelocity()).Normalize();
            bool bIsCloaker = false;
            bool bIsSniperOrTaser = false;
            bool bIsGrenadierOrTechie = false;
            bool bIsDozer = false;
            float flFOV{};
            {
                auto rotDelta = (SDK::UKismetMathLibrary::FindLookAtRotation(vecCameraLocation, vecTargetPosition).Normalize() - rotCameraRotation).Normalize();
                flFOV = std::sqrt(rotDelta.Yaw*rotDelta.Yaw + rotDelta.Pitch*rotDelta.Pitch);
            }

            if(flFOV > stConfig.m_flAimFOV)
                return{};
            
            if(pHuman->IsA(SDK::ACH_BaseCivilian_C::StaticClass()) || pHuman->bIsSurrendered){
                if(!stConfig.m_bCivilians)
                    return{};

                return TargetInfo_t{
                    .m_vecTargetPosition = vecTargetPosition,
                    .m_vecAimPosition = vecAimPosition,
                    .m_rotAimRotation = rotAimRotation,
                    .m_flPriority = (stConfig.m_eSorting == CheatConfig::Aimbot_t::ESorting::FOV) ? (360.f - flFOV) : 0.5f,
                    .m_flDistance = (pActor->K2_GetActorLocation() - vecCameraLocation).Magnitude(),
                    .m_iIndex = pHuman->Index,
                    .m_pEntity = pHuman->IsA(SDK::APawn::StaticClass()) ? reinterpret_cast<SDK::APawn*>(pHuman) : nullptr
                };
            }
            
            if(pHuman->IsA(SDK::ACH_Cloaker_C::StaticClass())){
                if(!stConfig.m_bSpecials)
                    return{};

                bIsCloaker = true;
            }
            else if(pHuman->IsA(SDK::ACH_Sniper_C::StaticClass()) || pHuman->IsA(SDK::ACH_Taser_C::StaticClass())){
                if(!stConfig.m_bSpecials)
                    return{};

                bIsSniperOrTaser = true;
            }
            else if(pHuman->IsA(SDK::ACH_Grenadier_C::StaticClass()) || pHuman->IsA(SDK::ACH_Tower_C::StaticClass())){
                if(!stConfig.m_bSpecials)
                    return{};

                bIsGrenadierOrTechie = true;
            }
            else if(pHuman->IsA(SDK::ACH_Dozer_C::StaticClass())){
                if(!stConfig.m_bSpecials)
                    return{};

                bIsDozer = true;

                vecAimPosition = vecTargetPosition + (SDK::UKismetMathLibrary::GetForwardVector((pHuman->Mesh->GetSocketRotation(nameHead) + SDK::FRotator(0.f, 90.f, 0.f)).Normalize()) * 50.f);
                rotAimRotation = SDK::UKismetMathLibrary::FindLookAtRotation(vecAimPosition, vecTargetPosition).Normalize();
            }
            else if(pHuman->IsA(SDK::ACH_SWAT_SHIELD_C::StaticClass())){
                if(!stConfig.m_bSpecials)
                    return{};
            }
            else{
                if(!stConfig.m_bGuards)
                    return{};
            }

            bool bTargetingLocalPlayer = pHuman->CurrentTarget && pHuman->CurrentTarget->Index == iLocalPlayerIndex; 
            bool bTargeting = pHuman->bIsTargeting;
            bool bInAction = false;
        
            if(auto pController = reinterpret_cast<SDK::ASBZAIController*>(pHuman->Controller); pController && pController->IsA(SDK::ASBZAIController::StaticClass())){
                bInAction = pController->CurrentActions.Num() > 0;
            }

            float flPriority = 0.f;
            if(stConfig.m_eSorting == CheatConfig::Aimbot_t::ESorting::FOV){
                flPriority = 360.f - flFOV;
            }
            else if(stConfig.m_eSorting == CheatConfig::Aimbot_t::ESorting::Threat){
                if(bInAction)
                    flPriority = 4.f;
                else if(bTargetingLocalPlayer)
                    flPriority = (bTargeting) ? 3.f : 2.f;
                else
                    flPriority = (bTargeting) ? 2.f : 1.f;
            }
            else{
                if(bIsCloaker)
                    flPriority = 11.f;
                else if(bIsSniperOrTaser){
                    if(bTargetingLocalPlayer)
                        flPriority = (bTargeting) ? 10.f : 5.f;
                    else
                        flPriority = (bTargeting) ? 5.f : 2.f;
                }
                else if(bIsGrenadierOrTechie)
                    flPriority = 8.f;
                else if(bIsDozer){
                    if(bInAction)
                        flPriority = 9.f;
                    else if(bTargetingLocalPlayer)
                        flPriority = (bTargeting) ? 7.f : 4.f;
                    else
                        flPriority = (bTargeting) ? 4.f : 2.f;
                }
                else{
                    if(bInAction)
                        flPriority = 9.f;
                    else if(bTargetingLocalPlayer)
                        flPriority = (bTargeting) ? 6.f : 3.f;
                    else
                        flPriority = (bTargeting) ? 3.f : 2.f;
                }
            }

            return TargetInfo_t{
                .m_vecTargetPosition = vecTargetPosition,
                .m_vecAimPosition = vecAimPosition,
                .m_rotAimRotation = rotAimRotation,
                .m_flPriority = flPriority,
                .m_flDistance = (pActor->K2_GetActorLocation() - vecCameraLocation).Magnitude(),
                .m_iIndex = pHuman->Index,
                .m_pEntity = pHuman->IsA(SDK::APawn::StaticClass()) ? reinterpret_cast<SDK::APawn*>(pHuman) : nullptr
            };
        };

        std::optional<TargetInfo_t> optBestTargetInfo{};
        const auto& actors1 = pGWorld->PersistentLevel->Actors;
        if(stConfig.m_bFBIVan){
            for(int i = 0; i < actors1.Num(); ++i){
                if(!actors1.IsValidIndex(i))
                    continue;

                auto pVan = reinterpret_cast<SDK::ABP_Van_FBI_C*>(actors1[i]);
                if(!pVan || !pVan->IsA(SDK::ABP_Van_FBI_C::StaticClass()))
                    continue;

                auto pMesh = pVan->AntennaHitMeshComponent;
                if(!pMesh || !pVan->bIsFBIActive)
                    continue;

                // Target and rotation.
                auto vecTargetPosition = pMesh->GetTightBounds(true).Origin;
                auto rot = (pMesh->K2_GetComponentRotation() + SDK::FRotator(180.f, 0.f, 0.f)).Normalize();

                float flFOV{};
                {
                    auto rotDelta = (SDK::UKismetMathLibrary::FindLookAtRotation(vecCameraLocation, vecTargetPosition).Normalize() - rotCameraRotation).Normalize();
                    flFOV = std::sqrt(rotDelta.Yaw*rotDelta.Yaw + rotDelta.Pitch*rotDelta.Pitch);
                }
                if(flFOV > stConfig.m_flAimFOV)
                    continue;

                optBestTargetInfo = TargetInfo_t{
                    .m_vecTargetPosition = vecTargetPosition,
                    .m_vecAimPosition = vecTargetPosition,
                    .m_rotAimRotation = rot,
                    .m_flPriority = (stConfig.m_eSorting == CheatConfig::Aimbot_t::ESorting::FOV) ? (360.f - flFOV) : 11.0f,
                    .m_flDistance = (vecTargetPosition - vecCameraLocation).Magnitude(),
                    .m_iIndex = pVan->Index,
                    .m_pEntity = nullptr
                };

                break;
            }
        }
        
        const auto& actors = pWorldRuntime->AllPawns->Objects;
        for(int i = 0; i < actors.Num(); ++i){
            if(!actors.IsValidIndex(i))
                break;

            auto pActor = reinterpret_cast<SDK::AActor*>(actors[i]);
            if(!pActor)
                continue;

            if(!stConfig.m_bThroughWalls && !pPlayerController->LineOfSightTo(pActor, SDK::FVector(0.f, 0.f, 0.f), false))
                continue;

            auto optTargetInfo = GetTargetInfo(pActor);
            if(!optTargetInfo)
                continue;

            if(!optBestTargetInfo)
                optBestTargetInfo = optTargetInfo;
            else if(optBestTargetInfo->m_flPriority < optTargetInfo->m_flPriority || (optBestTargetInfo->m_flPriority == optTargetInfo->m_flPriority && optBestTargetInfo->m_flDistance > optTargetInfo->m_flDistance))
                optBestTargetInfo = optTargetInfo;
        }

        g_stTargetInfo = optBestTargetInfo;
    }

    void AimbotOnFrameBegin(){
        if(!g_bIsInGame)
            return;

        SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
        if (!pGWorld)
            return;

        auto pGameInstance = reinterpret_cast<SDK::USBZGameInstance*>(pGWorld->OwningGameInstance);
        if (!pGameInstance || !pGameInstance->IsA(SDK::USBZGameInstance::StaticClass()))
            return;

        SDK::USBZWorldRuntime* pWorldRuntime = reinterpret_cast<SDK::USBZWorldRuntime*>(SDK::USBZWorldRuntime::GetWorldRuntime(pGWorld));
        if (!pWorldRuntime)
            return;

        if (!pGameInstance->LocalPlayers.IsValidIndex(0))
            return;

        SDK::ULocalPlayer* pULocalPlayer = pGameInstance->LocalPlayers[0];
        if (!pULocalPlayer)
            return;

        auto pLocalPlayerController = reinterpret_cast<SDK::ASBZPlayerController*>(pULocalPlayer->PlayerController);
        if (!pLocalPlayerController || !pLocalPlayerController->IsA(SDK::ASBZPlayerController::StaticClass()))
            return;

        GetOptimalAimbotTarget(pGWorld, pWorldRuntime, pLocalPlayerController);
    }
}
