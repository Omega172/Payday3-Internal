#include "Features.hpp"
#include "../Menu.hpp" 
#include "../Dumper-7/SDK.hpp"
#include "../Utils/Logging.hpp"
#include "./Misc/ClientMove.hpp"

#undef min
#undef max

void InstantInteraction(SDK::USBZPlayerInteractorComponent* pInteractor){
    static std::chrono::time_point<std::chrono::steady_clock> timeInteractLast = std::chrono::steady_clock::now();

    if(std::chrono::steady_clock::now() - timeInteractLast <= Cheat::g_durationPing || !pInteractor)
        return;

    auto pInteraction = pInteractor->GetCurrentInteraction();
    if(!pInteraction)
        return;

    if(auto pOwner = pInteraction->GetOwner(); pOwner && pOwner->IsA(SDK::ASBZBagItem::StaticClass()))
        return;
        

    pInteractor->Server_CompleteInteraction(pInteraction, pInteractor->InteractId);
    if(!Cheat::g_bIsSoloGame) // Makes the instant interaction look smoother in multiplayer
        pInteractor->Multicast_CompletedInteraction(pInteraction, false);

    // Dirty hack to prevent completing the interaction multiple times.
    timeInteractLast = std::chrono::steady_clock::now();
}

void InstantMinigame(SDK::ASBZPlayerState* pPlayerState){
    if(pPlayerState->MiniGameState != SDK::EPD3MiniGameState::None)
        pPlayerState->Server_SetMiniGameState(SDK::EPD3MiniGameState::Success);
}

struct WeaponDataBackupEntry_t{

    // Recoil
    float m_flViewSpeedDeflect;
    float m_flGunSpeedDeflect;

    // Spread
    float m_flInnerClusterSpreadMultiplier;
    float m_flFireSpreadStart;
    float m_flFireSpreadMinCap;
    float m_flFireSpreadCap;
    float m_flFireSpreadIncrease;

    // Fire
    uint32_t m_iProjectilesPerFiredRound;
    float m_flRoundsPerMinute;
    SDK::ESBZFireMode m_eFireMode;
};

bool g_bDidBackupWeaponData = false;
static std::map<uint64_t, WeaponDataBackupEntry_t> g_mapWeaponDataBackup{};

bool BackupWeaponData(SDK::USBZGameInstance* pGame){
    auto pDatabase = pGame->GlobalItemDatabase;
    if(!pDatabase)
        return false;

    SDK::USBZWeaponDatabase* aDatabases[3]{
        pDatabase->PrimaryWeapons.Get(),
        pDatabase->SecondaryWeapons.Get(),
        pDatabase->OverkillWeapons.Get()
    };

    if(!aDatabases[0] || !aDatabases[1] || !aDatabases[2])
        return false;

    for(int iDatabase = 0; iDatabase < 3; ++iDatabase){
        for(int i = 0; i < aDatabases[iDatabase]->Weapons.Num(); i++){
            if(!aDatabases[iDatabase]->Weapons.IsValidIndex(i))
                return false;

            auto pWeaponData = reinterpret_cast<SDK::USBZRangedWeaponData*>(aDatabases[iDatabase]->Weapons[i]);
            if(!pWeaponData)
                return false;

            if(!pWeaponData->IsA(SDK::USBZRangedWeaponData::StaticClass()))
                continue;

            if(!pWeaponData->SpreadData || !pWeaponData->RecoilData || !pWeaponData->FireData)
                return false;

            g_mapWeaponDataBackup.try_emplace(std::hash<std::string>{}(pWeaponData->Name.ToString().substr(14)), WeaponDataBackupEntry_t{
                .m_flViewSpeedDeflect = pWeaponData->RecoilData->ViewKick.SpeedDeflect,
                .m_flGunSpeedDeflect = pWeaponData->RecoilData->GunKickXY.SpeedDeflect,
                .m_flInnerClusterSpreadMultiplier = pWeaponData->SpreadData->InnerClusterSpreadMultiplier,
                .m_flFireSpreadStart = pWeaponData->SpreadData->FireSpreadStart,
                .m_flFireSpreadMinCap = pWeaponData->SpreadData->FireSpreadMinCap,
                .m_flFireSpreadCap = pWeaponData->SpreadData->FireSpreadCap,
                .m_flFireSpreadIncrease = pWeaponData->SpreadData->FireSpreadIncrease,
                .m_iProjectilesPerFiredRound = pWeaponData->FireData->ProjectilesPerFiredRound,
                .m_flRoundsPerMinute = pWeaponData->FireData->RoundsPerMinute,
                .m_eFireMode = pWeaponData->FireData->FireMode
            });
        }
    }

    return true;
}


void RestoreWeaponFire(SDK::USBZRangedWeaponData* pWeaponData){
    if(!pWeaponData || !g_bDidBackupWeaponData)
        return;

    auto pEquippable = pWeaponData->EquippableClass.Get();
    if(!pEquippable)
        return;

    std::string sName = pEquippable->Name.ToString().substr(16);
    sName.resize(sName.size() - 2);

    auto itrEntry = g_mapWeaponDataBackup.find(std::hash<std::string>{}(sName));
    if(itrEntry == g_mapWeaponDataBackup.end() || !pWeaponData->FireData)
        return;

    pWeaponData->FireData->ProjectilesPerFiredRound = itrEntry->second.m_iProjectilesPerFiredRound;
}

WeaponDataBackupEntry_t* GetWeaponBackupData(SDK::USBZRangedWeaponData* pWeaponData){
    if(!pWeaponData || !g_bDidBackupWeaponData)
        return{};

    auto pEquippable = pWeaponData->EquippableClass.Get();
    if(!pEquippable)
        return{};

    std::string sName = pEquippable->Name.ToString().substr(16);
    sName.resize(sName.size() - 2);

    auto itrEntry = g_mapWeaponDataBackup.find(std::hash<std::string>{}(sName));
    if(itrEntry == g_mapWeaponDataBackup.end())
        return{};

    return &itrEntry->second;
}

void ModifyWeaponData(SDK::USBZRangedWeaponData* pWeaponData){
    auto pBackupData = GetWeaponBackupData(pWeaponData);
    if(!pBackupData)
        return;

    if(pWeaponData->RecoilData){
        auto pRecoil = pWeaponData->RecoilData;
        if(CheatConfig::Get().m_misc.m_bNoRecoil)
            pRecoil->ViewKick.SpeedDeflect = pRecoil->GunKickXY.SpeedDeflect = 0.f;
        else if(pRecoil->ViewKick.SpeedDeflect == 0.f && pRecoil->GunKickXY.SpeedDeflect == 0.f){
            pWeaponData->RecoilData->ViewKick.SpeedDeflect = pBackupData->m_flViewSpeedDeflect;
            pWeaponData->RecoilData->GunKickXY.SpeedDeflect = pBackupData->m_flGunSpeedDeflect;
        }
    }

    if(pWeaponData->SpreadData){
        auto pSpread = pWeaponData->SpreadData;

        if(CheatConfig::Get().m_misc.m_bNoSpread)
            pSpread->InnerClusterSpreadMultiplier = pSpread->FireSpreadStart = pSpread->FireSpreadMinCap = pSpread->FireSpreadCap = pSpread->FireSpreadIncrease = 0.f;
        else if(pSpread->InnerClusterSpreadMultiplier == 0.f && pSpread->FireSpreadStart == 0.f && pSpread->FireSpreadMinCap == 0.f && pSpread->FireSpreadCap == 0.f && pSpread->FireSpreadIncrease == 0.f){
            pSpread->InnerClusterSpreadMultiplier = pBackupData->m_flInnerClusterSpreadMultiplier;
            pSpread->FireSpreadStart = pBackupData->m_flFireSpreadStart;
            pSpread->FireSpreadMinCap = pBackupData->m_flFireSpreadMinCap;
            pSpread->FireSpreadCap = pBackupData->m_flFireSpreadCap;
            pSpread->FireSpreadIncrease = pBackupData->m_flFireSpreadIncrease;
        }	
    }

    if(!pWeaponData->FireData)
        return;

    auto pFire = pWeaponData->FireData;
    if(CheatConfig::Get().m_misc.m_bMoreBullets)
        pFire->ProjectilesPerFiredRound = CheatConfig::Get().m_misc.m_iMoreBullets;
    else
        pFire->ProjectilesPerFiredRound = pBackupData->m_iProjectilesPerFiredRound;

    pFire->StartFireMinBuildup = 0.f;

    if(pWeaponData->TargetingData)
        pWeaponData->TargetingData->TargetingTransitionTime = 0.f;
    
    switch(CheatConfig::Get().m_misc.m_iRapidFire){
    case 1:
        pFire->RoundsPerMinute = std::max(500.f, pBackupData->m_flRoundsPerMinute);
        break;

    case 2:
        pFire->RoundsPerMinute = std::numeric_limits<float>::infinity();
        break;

    default:
        pFire->RoundsPerMinute = pBackupData->m_flRoundsPerMinute;
        break;
    }

    if(CheatConfig::Get().m_misc.m_bAutoPistol)
        pFire->FireMode = SDK::ESBZFireMode::Auto;
    else
        pFire->FireMode = pBackupData->m_eFireMode;

    //pFireData->MaximumPenetrationCount = std::numeric_limits<uint32_t>::max(); // Shoot through walls (Laggy af with more bullets)        
}

void LookForMethLabDialog(SDK::APD3HeistGameState* pGameState){
    if(!pGameState->DialogManager){
        Cheat::g_stMethLabInfo.m_iAnnouncedLab = -1;
        return;
    }

    // Muriatic Acid
    static auto namesha_rmk02_19 = SDK::UKismetStringLibrary::Conv_StringToName(L"sha_rmk02_19");
    static auto namesha_rmk02_16 = SDK::UKismetStringLibrary::Conv_StringToName(L"sha_rmk02_16");
    
    // Caustic Soda
    static auto namesha_rmk02_27 = SDK::UKismetStringLibrary::Conv_StringToName(L"sha_rmk02_27");
    static auto namesha_rmk02_17 = SDK::UKismetStringLibrary::Conv_StringToName(L"sha_rmk02_17");
    
    // Hcl
    static auto namesha_rmk02_21 = SDK::UKismetStringLibrary::Conv_StringToName(L"sha_rmk02_21");
    static auto namesha_rmk02_18 = SDK::UKismetStringLibrary::Conv_StringToName(L"sha_rmk02_18");
    
    // STOP
    static auto namesha_rmk02_25 = SDK::UKismetStringLibrary::Conv_StringToName(L"sha_rmk02_25");

    // Confirm
    static auto namesha_rmk02_29 = SDK::UKismetStringLibrary::Conv_StringToName(L"sha_rmk02_29");
    static auto namesha_rmk02_24 = SDK::UKismetStringLibrary::Conv_StringToName(L"sha_rmk02_24");

    if(Cheat::g_stMethLabInfo.m_iCorrectChoice != -1)
        return;

    auto& aActiveDialogs = pGameState->DialogManager->ActiveDialogs;
    for(int i = 0; i < aActiveDialogs.Num(); ++i){
        auto pDialog = aActiveDialogs[i].First;
        if(!pDialog)
            continue;

        auto& nameDialog = pDialog->Name;
        int8_t iChosenLab = -1;
        if(nameDialog == namesha_rmk02_19 || nameDialog == namesha_rmk02_16){
            iChosenLab = 0;
            if(Cheat::g_stMethLabInfo.m_bDidMu)
                continue;
        }else if(nameDialog == namesha_rmk02_27 || nameDialog == namesha_rmk02_17){
            iChosenLab = 1;
            if(Cheat::g_stMethLabInfo.m_bDidCs)
                continue;
        }else if(nameDialog == namesha_rmk02_21 || nameDialog == namesha_rmk02_18){
            iChosenLab = 2;
            if(Cheat::g_stMethLabInfo.m_bDidHcl)
                continue;
        }else if(nameDialog == namesha_rmk02_25){
            iChosenLab = -1;

            if(Cheat::g_stMethLabInfo.m_iAnnouncedLab != -1){
                uint8_t iRemaining = 3;
                if(Cheat::g_stMethLabInfo.m_bDidMu)
                    iRemaining--;

                if(Cheat::g_stMethLabInfo.m_bDidCs);
                    iRemaining--;

                if(Cheat::g_stMethLabInfo.m_bDidHcl)
                    iRemaining--;

                if(iRemaining == 2){
                    switch(Cheat::g_stMethLabInfo.m_iAnnouncedLab){
                    case(0):
                        Cheat::g_stMethLabInfo.m_iCorrectChoice = Cheat::g_stMethLabInfo.m_bDidCs ? 2 : 1;
                        break;
                    case(1):
                        Cheat::g_stMethLabInfo.m_iCorrectChoice = Cheat::g_stMethLabInfo.m_bDidMu ? 2 : 0;
                        break;
                    case(2):
                        Cheat::g_stMethLabInfo.m_iCorrectChoice = Cheat::g_stMethLabInfo.m_bDidCs ? 0 : 1;
                        break;
                    }
                    
                    Cheat::g_stMethLabInfo.m_iAnnouncedLab = -1;
                    return;
                }
                
            }
        }else if((nameDialog == namesha_rmk02_29 || nameDialog == namesha_rmk02_24) && Cheat::g_stMethLabInfo.m_iAnnouncedLab != -1){
            Cheat::g_stMethLabInfo.m_iCorrectChoice = Cheat::g_stMethLabInfo.m_iAnnouncedLab;
            Cheat::g_stMethLabInfo.m_iAnnouncedLab = -1;
        }

        if(Cheat::g_stMethLabInfo.m_iAnnouncedLab != iChosenLab && iChosenLab != -1){
            Cheat::g_stMethLabInfo.m_iAnnouncedLab = iChosenLab;
            Cheat::g_stMethLabInfo.m_timeAnnounced = std::chrono::steady_clock::now();
        }		
    }

    if(Cheat::g_stMethLabInfo.m_iAnnouncedLab == -1 || std::chrono::steady_clock::now() - Cheat::g_stMethLabInfo.m_timeAnnounced < std::chrono::seconds(12))
        return;

    Cheat::g_stMethLabInfo.m_iCorrectChoice = Cheat::g_stMethLabInfo.m_iAnnouncedLab;
    Cheat::g_stMethLabInfo.m_iAnnouncedLab = -1;
}

void OverrideMethLabInteractables(){
    if(Cheat::g_iMethLabIndex <= 0 || Cheat::g_iMethLabIndex > SDK::UObject::GObjects->Num())
        return;

    auto pLab = reinterpret_cast<SDK::ASBZCookingStation*>(SDK::UObject::GObjects->GetByIndex(Cheat::g_iMethLabIndex));
    if(!pLab || !pLab->IsA(SDK::ASBZCookingStation::StaticClass()))
        return;

    if(pLab->CurrentState != SDK::ESBZCookingState::WaitingForIngredients)
        Cheat::g_stMethLabInfo.m_bDidMu = Cheat::g_stMethLabInfo.m_bDidCs = Cheat::g_stMethLabInfo.m_bDidHcl = false;

    if(Cheat::g_stMethLabInfo.m_iCorrectChoice == -1){
        if(Cheat::g_stMethLabInfo.m_bDidCs && Cheat::g_stMethLabInfo.m_bDidHcl)
            Cheat::g_stMethLabInfo.m_iCorrectChoice = 0;
        else if(Cheat::g_stMethLabInfo.m_bDidMu && Cheat::g_stMethLabInfo.m_bDidHcl)
            Cheat::g_stMethLabInfo.m_iCorrectChoice = 1;
        else if(Cheat::g_stMethLabInfo.m_bDidCs && Cheat::g_stMethLabInfo.m_bDidMu)
            Cheat::g_stMethLabInfo.m_iCorrectChoice = 2;
    }

    bool bDisabled = false;
    for(int i = 0; i < pLab->IngredientInteractableArray.Num(); ++i){
        auto pInteractable = pLab->IngredientInteractableArray[i];
        pInteractable->SetLocalEnabled(i == Cheat::g_stMethLabInfo.m_iCorrectChoice);

        if(!pInteractable->bInteractionEnabled)
            bDisabled = true;
    }



    if(bDisabled && !Cheat::g_stMethLabInfo.m_bWasDisabled){
        if(Cheat::g_stMethLabInfo.m_iCorrectChoice != -1){
            switch(Cheat::g_stMethLabInfo.m_iCorrectChoice){
            case(0):
                Cheat::g_stMethLabInfo.m_bDidMu = true;
                break;
            case(1):
                Cheat::g_stMethLabInfo.m_bDidCs = true;
                break;
            case(2):
                Cheat::g_stMethLabInfo.m_bDidHcl = true;
                break;
            }

            Cheat::g_stMethLabInfo.m_iAnnouncedLab = -1;
        }

        Cheat::g_stMethLabInfo.m_iCorrectChoice = Cheat::g_stMethLabInfo.m_iAnnouncedLab = -1;
    }
        
    
    Cheat::g_stMethLabInfo.m_bWasDisabled = bDisabled;
}


void Cheat::OnPlayerControllerTick(){
    SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
	if (!pGWorld)
		return;

    auto pGameInstance = reinterpret_cast<SDK::USBZGameInstance*>(pGWorld->OwningGameInstance);
	if (!pGameInstance || !pGameInstance->IsA(SDK::USBZGameInstance::StaticClass()))
		return;

    SDK::USBZWorldRuntime* pWorldRuntime = reinterpret_cast<SDK::USBZWorldRuntime*>(SDK::USBZWorldRuntime::GetWorldRuntime(pGWorld));
    if (!pWorldRuntime)
        return;

    auto pGameState = reinterpret_cast<SDK::APD3HeistGameState*>(pGWorld->GameState);
    if(pGameState && pGameState->IsA(SDK::APD3HeistGameState::StaticClass())){
        //LookForMethLabDialog(pGameState);
        //OverrideMethLabInteractables();
    }

    g_bIsSoloGame = SDK::USBZOnlineFunctionLibrary::IsSoloGame(pGWorld);

    if(!g_bDidBackupWeaponData)
        g_bDidBackupWeaponData = BackupWeaponData(pGameInstance);

	if (!pGameInstance->LocalPlayers.IsValidIndex(0))
		return;

	SDK::ULocalPlayer* pULocalPlayer = pGameInstance->LocalPlayers[0];
	if (!pULocalPlayer)
		return;

	auto pLocalPlayerController = reinterpret_cast<SDK::ASBZPlayerController*>(pULocalPlayer->PlayerController);
	if (!pLocalPlayerController || !pLocalPlayerController->IsA(SDK::ASBZPlayerController::StaticClass()))
		return;

    if(SDK::USBZSettingsFunctionsVideo::GetCameraVerticalFieldOfView(pGWorld) != CheatConfig::Get().m_misc.m_flCameraFOV)
        SDK::USBZSettingsFunctionsVideo::SetCameraVerticalFieldOfView(pGWorld, CheatConfig::Get().m_misc.m_flCameraFOV);
    
    //pLocalPlayerController->ServerChangeName(SDK::FString(LR"()"));

	auto pLocalPlayer = reinterpret_cast<SDK::ASBZPlayerCharacter*>(pLocalPlayerController->AcknowledgedPawn);
	if (!pLocalPlayer || !pLocalPlayer->IsA(SDK::ASBZPlayerCharacter::StaticClass()))
		return;

    if(CheatConfig::Get().m_misc.m_bNoFallDamage)
        pLocalPlayer->FallingStartHeight = pLocalPlayer->K2_GetActorLocation().Z;

    if(CheatConfig::Get().m_misc.m_bInstantInteraction)
        InstantInteraction(pLocalPlayer->Interactor);

	if (pLocalPlayer->SBZPlayerState)
	{
		auto pPlayerState = pLocalPlayer->SBZPlayerState;
        g_durationPing = std::chrono::milliseconds(std::min(static_cast<int>(pPlayerState->GetPingInMilliseconds() * 2.f), 30));

		if (CheatConfig::Get().m_misc.m_bInstantMinigame)
			InstantMinigame(pPlayerState);
	}

    if (pLocalPlayer->PlayerAbilitySystem)
    {
        static SDK::FGameplayTag tagDummy{
            SDK::UKismetStringLibrary::Conv_StringToName(L"")
        };

        auto pAbilitySystem = pLocalPlayer->PlayerAbilitySystem;
        auto& aAbilities = pAbilitySystem->ActivatableAbilities.Items;
        
        static bool bDidHaveSpeedBuff = false;
        bool bSpeedBuff = CheatConfig::Get().m_misc.m_bSpeedBuff;
        if(bSpeedBuff)
            pAbilitySystem->Server_SetSpeedBuffTime(tagDummy, 99999.f);
        else if(bDidHaveSpeedBuff)
            pAbilitySystem->Server_ResetSpeedBuffTime();
        bDidHaveSpeedBuff = bSpeedBuff;

        static bool bDidHaveDamageBuff = false;
        bool bDamageBuff = CheatConfig::Get().m_misc.m_bDamageBuff;
        if(bDamageBuff)
            pAbilitySystem->Server_SetDamageBuffTime(tagDummy, 99999.f);
        else if(bDidHaveDamageBuff)
            pAbilitySystem->Server_ResetDamageBuffTime();
        bDidHaveDamageBuff = bDamageBuff;

        static bool bDidHaveArmorBuff = false;
        bool bArmorBuff = CheatConfig::Get().m_misc.m_bArmorBuff;
        if(bArmorBuff)
            pAbilitySystem->Server_SetMitigationBuffTime(tagDummy, 99999.f);
        else if(bDidHaveArmorBuff)
            pAbilitySystem->Server_ResetMitigationBuffTime();
        bDidHaveArmorBuff = bArmorBuff;

        static auto nameDefault__GA_Fire_C = SDK::UKismetStringLibrary::Conv_StringToName(L"Default__GA_Fire_C");
        static auto nameDefault__GA_Reload_C = SDK::UKismetStringLibrary::Conv_StringToName(L"Default__GA_Reload_C");
        static auto nameDefault__GA_Run_C = SDK::UKismetStringLibrary::Conv_StringToName(L"Default__GA_Run_C");
        for (int i = 0; i < aAbilities.Num(); ++i) {
            if (!aAbilities.IsValidIndex(i))
                continue;

            auto pAbility = aAbilities[i].Ability;
            if (!pAbility)
                continue;

            pAbilitySystem->Client_UnblockAbility(aAbilities[i].Handle);

            if (pAbility->Name == nameDefault__GA_Fire_C){
                Cheat::g_iFireAbilityHandle = aAbilities[i].Handle.Handle;
            }
            if (pAbility->Name == nameDefault__GA_Run_C){
                //pAbilitySystem->ServerSetInputPressed(aAbilities[i].Handle);
                //pAbilitySystem->ClientTryActivateAbility(aAbilities[i].Handle);
            }

            if (pAbility->Name == nameDefault__GA_Reload_C){
                
                //pAbilitySystem->ServerCancelAbility(aAbilities[i].Handle, pAbility->CurrentActivationInfo);
            }

            //GetCurrentMontage()
            //pAbilitySystem->ServerSetInputPressed(aAbilities[i].Handle);
            //pAbilitySystem->ClientTryActivateAbility(aAbilities[i].Handle);
        }
    }

	if (pLocalPlayer->FPCameraAttachment && pLocalPlayer->FPCameraAttachment->EquippedWeaponData){
		if (pLocalPlayer->FPCameraAttachment->EquippedWeaponData->IsA(SDK::USBZRangedWeaponData::StaticClass())){
			auto pWeaponData = reinterpret_cast<SDK::USBZRangedWeaponData*>(pLocalPlayer->FPCameraAttachment->EquippedWeaponData);
            ModifyWeaponData(pWeaponData);
		}

        if(pLocalPlayer->FPCameraAttachment->EquippedWeapon->IsA(SDK::ASBZRangedWeapon::StaticClass())){
            auto pWeapon = reinterpret_cast<SDK::ASBZRangedWeapon*>(pLocalPlayer->FPCameraAttachment->EquippedWeapon);

        }
	}

    SDK::USBZPlayerMovementComponent* pMovementComponent = reinterpret_cast<SDK::USBZPlayerMovementComponent*>(pLocalPlayer->GetComponentByClass(SDK::USBZPlayerMovementComponent::StaticClass()));
    if (!pMovementComponent)
        return;

    ClientMove::OnPlayerControllerTick(pLocalPlayer, pMovementComponent);

    /**
     *     else if(pLocalPlayer->GetLastKnownRoom() != pLocalPlayer->GetCurrentRoom_Implementation() && pLocalPlayer->GetCurrentRoom_Implementation()){
        SDK::FVector vecPoint = pLocalPlayer->K2_GetActorLocation();
        SDK::FSBZMinimalAgilityTraversalTrajectory trajectory{
            vecPoint, vecPoint, vecPoint, vecPoint, std::numeric_limits<int16_t>::max(), SDK::ESBZAgilityTraversalType::VaultLowFast, false, false
        };

        pMovementComponent->Server_StartTraversal(trajectory);
        g_bForceMoveForTeleport = true;
        timeTeleportLast = std::chrono::steady_clock::now();
    }
     */

    if(CheatConfig::Get().m_misc.m_bNoCameraShake)
	    pLocalPlayerController->PlayerCameraManager->StopAllCameraShakes(true);

    if(pLocalPlayer->TiltCameraModifier){
        if(CheatConfig::Get().m_misc.m_bNoCameraTilt)
            pLocalPlayer->TiltCameraModifier->DisableModifier(true);  
        else if(pLocalPlayer->TiltCameraModifier->IsDisabled())
            pLocalPlayer->TiltCameraModifier->EnableModifier();
    }

    static auto nameSBZFireKickBackCameraModifier = SDK::UKismetStringLibrary::Conv_StringToName(L"SBZFireKickBackCameraModifier");
    if(pLocalPlayerController->PlayerCameraManager && pLocalPlayerController->PlayerCameraManager->IsA(SDK::ASBZPlayerCameraManager::StaticClass())){
        auto pCameraManager = reinterpret_cast<SDK::ASBZPlayerCameraManager*>(pLocalPlayerController->PlayerCameraManager);
        for(int i = 0; i < pCameraManager->SBZCameraModifierList.Num(); ++i){
            auto pModifier = pCameraManager->SBZCameraModifierList[i];
            if(!pModifier || pModifier->Name != nameSBZFireKickBackCameraModifier)
                continue;

            if(CheatConfig::Get().m_misc.m_bNoCameraShake)
                pModifier->DisableModifier(true);
            else if(pModifier->IsDisabled())
                pModifier->EnableModifier();
        }
    }   

    if(pLocalPlayer->LastLocalReloadMontage)
        pLocalPlayer->LastLocalReloadMontage->RateScale = CheatConfig::Get().m_misc.m_bInstantReload ? 10000000.f : 1.f;

    if(pLocalPlayer->CurrentMeleeMontage)
        pLocalPlayer->CurrentMeleeMontage->RateScale = CheatConfig::Get().m_misc.m_bInstantMelee ? 10000000.f : 1.f;
}