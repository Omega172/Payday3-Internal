#include "pch.h"
#include "Player.hpp"
#include <vector>
#include <algorithm>

namespace
{
	SDK::ASBZPlayerCharacter* GetLocalCharacter()
	{
		return reinterpret_cast<SDK::ASBZPlayerCharacter*>(Unreal::GetAcknowledgedPawn());
	}
}

void Player::HandleMenu()
{
	static std::once_flag onceflag;

	std::call_once(onceflag, [this]() {
		auto pHeaderGroup = static_cast<HeaderGroup*>(Framework::menu->GetChild("HEADER_GROUP"));
		if (pHeaderGroup)
			pHeaderGroup->AddHeaders(Player::s_iPlayerPageId, { "PLAYER_TAB1"Hashed, "PLAYER_TAB2"Hashed, "PLAYER_TAB3"Hashed });

		m_pTab1Left->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() - 20.0f));
		});
		m_pTab1Right->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() - 20.0f));
		});
		m_pTab2Left->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 20.0f), (ImGui::GetWindowHeight() - 20.0f));
		});
		m_pTab3Left->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() - 20.0f));
		});
		m_pTab3Right->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() - 20.0f));
		});

		// Tab 1
		m_pTab1Left->AddElement(m_pGodMode.get());
		m_pTab1Left->AddElement(m_pInfStamina.get());
		m_pTab1Left->AddElement(m_pInstaMelee.get());
		m_pTab1Left->AddElement(m_pNoScreenshake.get());
		m_pTab1Left->AddElement(m_pNoFallDamage.get());
		m_pTab1Left->AddElement(m_pNoDetection.get());

		m_pTab1Right->AddElement(m_pGodModeType.get());
		m_pGodModeType->AddOption("Set");
		m_pGodModeType->AddOption("Block");

		m_pTab1Group->AddElement(m_pTab1Left.get());
		m_pTab1Group->AddElement(m_pTab1Right.get());

		// Tab 2 - population logic not implemented, placeholder rows only
		m_pTab2Left->AddElement(m_pPlayerTable.get());
		m_pPlayerTable->AddColumn("Name");
		m_pPlayerTable->AddColumn("Health");
		m_pPlayerTable->AddColumn("Distance");
		m_pPlayerTable->AddColumn("Status");
		m_pPlayerTable->AddRow({ "Omega", "100", "125m", "Alive" });
		m_pPlayerTable->AddRow({ "Bob", "75", "84m", "Alive" });
		m_pPlayerTable->SetSaveToConfig(false);

		m_pTab2Group->AddElement(m_pTab2Left.get());

		// Tab 3
		m_pTab3Left->AddElement(m_pInstaReload.get());
		m_pTab3Left->AddElement(m_pInfAmmo.get());
		m_pTab3Left->AddElement(m_pNoRecoil.get());
		m_pTab3Left->AddElement(m_pNoSpread.get());
		m_pTab3Left->AddElement(m_pFireRate.get());
		m_pTab3Right->AddElement(m_pFireRateSlider.get());

		m_pTab3Group->AddElement(m_pTab3Left.get());
		m_pTab3Group->AddElement(m_pTab3Right.get());

		m_pTab1Page->AddElement(m_pTab1Group.get());
		m_pTab2Page->AddElement(m_pTab2Group.get());
		m_pTab3Page->AddElement(m_pTab3Group.get());

		Framework::menu->GetChild("HEADER_GROUP")->GetChild("BODY")->AddElement(m_pTab1Page.get());
		Framework::menu->GetChild("HEADER_GROUP")->GetChild("BODY")->AddElement(m_pTab2Page.get());
		Framework::menu->GetChild("HEADER_GROUP")->GetChild("BODY")->AddElement(m_pTab3Page.get());
	});
}

void Player::blockDamage(bool bEnabled)
{
	auto* localChar = GetLocalCharacter();
	if (!localChar)
	{
		m_bBlockDamageApplied = false;
		return;
	}

	if (bEnabled)
	{
		if (!m_bBlockDamageApplied)
		{
			m_bOriginalCanBeDamaged = localChar->bCanBeDamaged;
			m_bBlockDamageApplied = false;
		}

		localChar->bCanBeDamaged = false;
		m_bBlockDamageApplied = true;
	}
	else if (m_bBlockDamageApplied)
	{
		localChar->bCanBeDamaged = m_bOriginalCanBeDamaged;
		m_bBlockDamageApplied = false;
	}
}

void Player::InstantMelee(bool bEnabled)
{
	auto* localChar = GetLocalCharacter();
	if (!localChar)
		return;

	auto* montage = localChar->CurrentMeleeMontage;
	if (!montage)
	{
		m_pLastMeleeMontage = nullptr;
		m_bInstantMeleeApplied = false;
		return;
	}

	if (bEnabled)
	{
		if (montage != m_pLastMeleeMontage)
		{
			m_pLastMeleeMontage = montage;
			m_flOriginalMeleeRate = montage->RateScale;
			m_bInstantMeleeApplied = false;
		}

		if (!m_bInstantMeleeApplied || montage->RateScale != 10000.0f)
		{
			montage->RateScale = 10000.0f;

			if (montage->RateScale == 10000.0f)
				m_bInstantMeleeApplied = true;
		}
	}
	else if (m_bInstantMeleeApplied)
	{
		montage->RateScale = m_flOriginalMeleeRate;

		m_bInstantMeleeApplied = false;
		m_pLastMeleeMontage = nullptr;
	}
}

void Player::InstantReload(bool bEnabled)
{
	auto* localChar = GetLocalCharacter();
	if (!localChar)
		return;

	auto* montage = localChar->LastLocalReloadMontage;
	if (!montage)
	{
		m_pLastReloadMontage = nullptr;
		m_bInstantReloadApplied = false;
		return;
	}

	if (bEnabled)
	{
		if (montage != m_pLastReloadMontage)
		{
			m_pLastReloadMontage = montage;
			m_flOriginalReloadRate = montage->RateScale;
			m_bInstantReloadApplied = false;
		}

		if (!m_bInstantReloadApplied || montage->RateScale != 10000.0f)
		{
			montage->RateScale = 10000.0f;

			if (montage->RateScale == 10000.0f)
				m_bInstantReloadApplied = true;
		}
	}
	else if (m_bInstantReloadApplied)
	{
		montage->RateScale = m_flOriginalReloadRate;

		m_bInstantReloadApplied = false;
		m_pLastReloadMontage = nullptr;
	}
}

bool BackupAttrData()
{
	auto* localChar = GetLocalCharacter();
	if (!localChar)
		return false;

	auto* PlayerAttributeSet = localChar->PlayerAttributeSet;
	if (!PlayerAttributeSet)
		return false;

	g_attrDataBackup.try_emplace(std::hash<std::string>{}(localChar->Name.ToString()), AttrDataBackupEntry_t{
		.m_flHealth = PlayerAttributeSet->HealthMax,
		.m_flStamina = PlayerAttributeSet->Stamina,

		.m_flPrimaryEquippableAmmoInventory = PlayerAttributeSet->PrimaryEquippableAmmoInventory,
		.m_flSecondaryEquippableAmmoInventory = PlayerAttributeSet->SecondaryEquippableAmmoInventory,
		.m_flTertiaryEquippableAmmoInventory = PlayerAttributeSet->TertiaryEquippableAmmoInventory,

		.m_flPrimaryThrowableAmmoInventory = PlayerAttributeSet->PrimaryThrowableAmmoInventory,
		.m_flSecondaryThrowableAmmoInventory = PlayerAttributeSet->SecondaryThrowableAmmoInventory,
		.m_flTertiaryThrowableAmmoInventory = PlayerAttributeSet->TertiaryThrowableAmmoInventory,

		.m_flPrimaryToolAmmoInventory = PlayerAttributeSet->PrimaryToolAmmoInventory,
		.m_flSecondaryToolAmmoInventory = PlayerAttributeSet->SecondaryToolAmmoInventory,
		.m_flTertiaryToolAmmoInventory = PlayerAttributeSet->TertiaryToolAmmoInventory
	});
	return true;
}

AttrDataBackupEntry_t* Player::GetAttrBackupData(SDK::ASBZPlayerCharacter* pLocalChar)
{
	if (!pLocalChar)
		return nullptr;

	size_t hash = std::hash<std::string>{}(pLocalChar->Name.ToString());

	auto itrEntry = g_attrDataBackup.find(hash);
	if (itrEntry == g_attrDataBackup.end())
		return nullptr;

	return &itrEntry->second;
}

bool BackupWeaponData()
{
	SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
	if (!pGWorld)
		return false;

	auto pGameInstance = reinterpret_cast<SDK::USBZGameInstance*>(pGWorld->OwningGameInstance);
	if (!pGameInstance || !pGameInstance->IsA(SDK::USBZGameInstance::StaticClass()))
		return false;

	auto pDatabase = pGameInstance->GlobalItemDatabase;
	if (!pDatabase)
		return false;

	SDK::USBZWeaponDatabase* aDatabases[3]{
		pDatabase->PrimaryWeapons.Get(),
		pDatabase->SecondaryWeapons.Get(),
		pDatabase->OverkillWeapons.Get()
	};

	if (!aDatabases[0] || !aDatabases[1] || !aDatabases[2])
		return false;

	for (int iDatabase = 0; iDatabase < 3; ++iDatabase)
	{
		for (int i = 0; i < aDatabases[iDatabase]->Weapons.Num(); i++)
		{
			if (!aDatabases[iDatabase]->Weapons.IsValidIndex(i))
				return false;

			auto pWeaponData = reinterpret_cast<SDK::USBZRangedWeaponData*>(aDatabases[iDatabase]->Weapons[i]);
			if (!pWeaponData)
				return false;

			if (!pWeaponData->IsA(SDK::USBZRangedWeaponData::StaticClass()))
				continue;

			if (!pWeaponData->SpreadData || !pWeaponData->RecoilData || !pWeaponData->FireData)
				return false;

			g_mapWeaponDataBackup.try_emplace(std::hash<std::string>{}(pWeaponData->Name.ToString().substr(14)), WeaponDataBackupEntry_t{
				.m_flViewSpeedDeflect = pWeaponData->RecoilData->ViewKick.SpeedDeflect,
				.m_flGunKickBackSpeedDeflect = pWeaponData->RecoilData->GunKickBack.SpeedDeflect,
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

WeaponDataBackupEntry_t* Player::GetWeaponBackupData(SDK::USBZRangedWeaponData* pWeaponData)
{
	if (!pWeaponData)
		return nullptr;

	auto* pEquippable = pWeaponData->EquippableClass.Get();
	if (!pEquippable)
		return nullptr;

	std::string sName = pEquippable->Name.ToString().substr(16);
	sName.resize(sName.size() - 2);

	size_t hash = std::hash<std::string>{}(sName);

	auto itrEntry = g_mapWeaponDataBackup.find(hash);
	if (itrEntry == g_mapWeaponDataBackup.end())
		return nullptr;

	return &itrEntry->second;
}

std::vector<SDK::USBZRangedWeaponData*> Player::GetCurrentWeaponData(SDK::ASBZPlayerCharacter* pLocalChar)
{
	std::vector<SDK::USBZRangedWeaponData*> weapons;

	if (pLocalChar->FPCameraAttachment)
	{
		auto* equippedData = pLocalChar->FPCameraAttachment->EquippedWeaponData;
		if (equippedData && equippedData->IsA(SDK::USBZRangedWeaponData::StaticClass()))
			weapons.push_back(reinterpret_cast<SDK::USBZRangedWeaponData*>(equippedData));
	}

	for (UC::int32 i = 0; i < pLocalChar->EquippableArray.Num(); ++i)
	{
		if (!pLocalChar->EquippableArray.IsValidIndex(i))
			continue;

		auto* equippable = pLocalChar->EquippableArray[i];
		if (!equippable)
			continue;

		auto* weaponData = equippable->EquippableConfig.EquippableData;
		if (weaponData && weaponData->IsA(SDK::USBZRangedWeaponData::StaticClass()))
		{
			auto* rangedData = reinterpret_cast<SDK::USBZRangedWeaponData*>(weaponData);
			if (std::find(weapons.begin(), weapons.end(), rangedData) == weapons.end())
				weapons.push_back(rangedData);
		}
	}
	return weapons;
}

void Player::noRecoil(bool bEnabled)
{
	SDK::ASBZPlayerCharacter* localChar = GetLocalCharacter();
	if (!localChar || !g_bDidBackupWeaponData)
		return;

	for (auto* weaponData : GetCurrentWeaponData(localChar))
	{
		if (!weaponData || !weaponData->RecoilData)
			continue;

		if (bEnabled)
		{
			weaponData->RecoilData->ViewKick.SpeedDeflect = 0.f;
			weaponData->RecoilData->GunKickBack.SpeedDeflect = 0.f;
			weaponData->RecoilData->GunKickXY.SpeedDeflect = 0.f;
		}
		else
		{
			auto* backup = GetWeaponBackupData(weaponData);
			if (backup)
			{
				weaponData->RecoilData->ViewKick.SpeedDeflect = backup->m_flViewSpeedDeflect;
				weaponData->RecoilData->GunKickBack.SpeedDeflect = backup->m_flGunKickBackSpeedDeflect;
				weaponData->RecoilData->GunKickXY.SpeedDeflect = backup->m_flGunSpeedDeflect;
			}
		}
	}
}

void Player::noSpread(bool bEnabled)
{
	SDK::ASBZPlayerCharacter* localChar = GetLocalCharacter();
	if (!localChar || !g_bDidBackupWeaponData)
		return;

	for (auto* weaponData : GetCurrentWeaponData(localChar))
	{
		if (!weaponData || !weaponData->SpreadData)
			continue;

		if (bEnabled)
		{
			weaponData->SpreadData->InnerClusterSpreadMultiplier =
				weaponData->SpreadData->FireSpreadStart =
				weaponData->SpreadData->FireSpreadMinCap =
				weaponData->SpreadData->FireSpreadCap =
				weaponData->SpreadData->FireSpreadIncrease = 0.f;
		}
		else
		{
			auto* backup = GetWeaponBackupData(weaponData);
			if (backup)
			{
				weaponData->SpreadData->InnerClusterSpreadMultiplier = backup->m_flInnerClusterSpreadMultiplier;
				weaponData->SpreadData->FireSpreadStart = backup->m_flFireSpreadStart;
				weaponData->SpreadData->FireSpreadMinCap = backup->m_flFireSpreadMinCap;
				weaponData->SpreadData->FireSpreadCap = backup->m_flFireSpreadCap;
				weaponData->SpreadData->FireSpreadIncrease = backup->m_flFireSpreadIncrease;
			}
		}
	}
}

void Player::fireRate(bool bEnabled)
{
	SDK::ASBZPlayerCharacter* localChar = GetLocalCharacter();
	if (!localChar || !g_bDidBackupWeaponData)
		return;

	for (auto* weaponData : GetCurrentWeaponData(localChar))
	{
		if (!weaponData || !weaponData->FireData)
			continue;

		auto* backup = GetWeaponBackupData(weaponData);
		if (bEnabled)
		{
			if (backup)
			{
				weaponData->FireData->RoundsPerMinute = backup->m_flRoundsPerMinute * m_pFireRateSlider->GetValue();
				weaponData->FireData->FireMode = SDK::ESBZFireMode::Auto;
			}
		}
		else if (backup)
		{
			weaponData->FireData->RoundsPerMinute = backup->m_flRoundsPerMinute;
			weaponData->FireData->FireMode = backup->m_eFireMode;
		}
	}
}

void Player::Run()
{
	if (!g_bDidBackupWeaponData)
		g_bDidBackupWeaponData = BackupWeaponData();

	if (!g_bDidBackupAttrData)
		g_bDidBackupAttrData = BackupAttrData();

	const bool bGodMode = m_pGodMode->GetValue();
	const int iGodModeType = m_pGodModeType->GetSelectedIndex();
	const bool bInfAmmo = m_pInfAmmo->GetValue();

	if (m_bPrevGodMode && (!bGodMode || iGodModeType != m_iPrevGodModeType))
	{
		if (auto* localChar = GetLocalCharacter())
		{
			if (m_iPrevGodModeType == 0)
			{
				auto* playerAttributeSet = localChar->PlayerAttributeSet;
				auto backup = GetAttrBackupData(localChar);
				if (playerAttributeSet && backup)
					playerAttributeSet->Health.CurrentValue = backup->m_flHealth.CurrentValue;
			}
			else if (m_iPrevGodModeType == 1)
			{
				blockDamage(false);
			}
		}
	}

	if (m_bPrevInfAmmo && !bInfAmmo)
	{
		if (auto* localChar = GetLocalCharacter())
		{
			auto* PlayerAttributeSet = localChar->PlayerAttributeSet;
			auto backup = GetAttrBackupData(localChar);
			if (PlayerAttributeSet && backup)
			{
				PlayerAttributeSet->PrimaryEquippableAmmoInventory.CurrentValue = backup->m_flPrimaryEquippableAmmoInventory.CurrentValue;
				PlayerAttributeSet->SecondaryEquippableAmmoInventory.CurrentValue = backup->m_flSecondaryEquippableAmmoInventory.CurrentValue;
				PlayerAttributeSet->TertiaryEquippableAmmoInventory.CurrentValue = backup->m_flTertiaryEquippableAmmoInventory.CurrentValue;

				PlayerAttributeSet->PrimaryThrowableAmmoInventory.CurrentValue = backup->m_flPrimaryThrowableAmmoInventory.CurrentValue;
				PlayerAttributeSet->SecondaryThrowableAmmoInventory.CurrentValue = backup->m_flSecondaryThrowableAmmoInventory.CurrentValue;
				PlayerAttributeSet->TertiaryThrowableAmmoInventory.CurrentValue = backup->m_flTertiaryThrowableAmmoInventory.CurrentValue;

				PlayerAttributeSet->PrimaryToolAmmoInventory.CurrentValue = backup->m_flPrimaryToolAmmoInventory.CurrentValue;
				PlayerAttributeSet->SecondaryToolAmmoInventory.CurrentValue = backup->m_flSecondaryToolAmmoInventory.CurrentValue;
				PlayerAttributeSet->TertiaryToolAmmoInventory.CurrentValue = backup->m_flTertiaryToolAmmoInventory.CurrentValue;
			}
		}
	}

	m_bPrevGodMode = bGodMode;
	m_iPrevGodModeType = iGodModeType;
	m_bPrevInfAmmo = bInfAmmo;

	// Tab 1

	if (bGodMode)
	{
		if (auto* localChar = GetLocalCharacter())
		{
			if (iGodModeType == 0)
			{
				auto* PlayerAttributeSet = localChar->PlayerAttributeSet;
				PlayerAttributeSet->Health.CurrentValue = PlayerAttributeSet->HealthMax.CurrentValue * 10;
			}
			if (iGodModeType == 1)
				blockDamage(true);
		}
	}

	if (m_pInfStamina->GetValue())
	{
		if (auto* localChar = GetLocalCharacter())
		{
			auto* PlayerAttributeSet = localChar->PlayerAttributeSet;
			PlayerAttributeSet->Stamina.CurrentValue = 100.0f;
		}
	}

	InstantMelee(m_pInstaMelee->GetValue());

	if (m_pNoScreenshake->GetValue())
	{
		auto* camManager = Unreal::GetPlayerCameraManager();
		if (camManager)
			camManager->StopAllCameraShakes(m_pNoScreenshake->GetValue());
	}

	if (m_pNoFallDamage->GetValue())
	{
		if (auto* localChar = GetLocalCharacter())
			localChar->FallingStartHeight = localChar->K2_GetActorLocation().Z;
	}

	if (m_pNoDetection->GetValue())
	{
		if (auto* localChar = GetLocalCharacter())
		{
			for (UC::int32 i = 0; i < localChar->VisualDetectors.Num(); ++i)
			{
				auto* det = localChar->VisualDetectors[i];
				if (!det)
					continue;

				for (UC::int32 e = 0; e < det->EnemyDetectionValue.Num(); ++e)
					det->EnemyDetectionValue[e].Target = nullptr;

				det->bMarkAsCriminalOnSearch = false;
				det->bShouldDisplayDetectionBuildup = false;
			}
		}
	}

	// Tab 3

	InstantReload(m_pInstaReload->GetValue());

	if (bInfAmmo)
	{
		if (auto* localChar = GetLocalCharacter())
		{
			auto* Att = localChar->PlayerAttributeSet;

			Att->PrimaryEquippableAmmoInventory.CurrentValue = 999;
			Att->SecondaryEquippableAmmoInventory.CurrentValue = 999;
			Att->TertiaryEquippableAmmoInventory.CurrentValue = 999;

			Att->PrimaryThrowableAmmoInventory.CurrentValue = 999;
			Att->SecondaryThrowableAmmoInventory.CurrentValue = 999;
			Att->TertiaryThrowableAmmoInventory.CurrentValue = 999;

			Att->PrimaryToolAmmoInventory.CurrentValue = 999;
			Att->SecondaryToolAmmoInventory.CurrentValue = 999;
			Att->TertiaryToolAmmoInventory.CurrentValue = 999;
		}
	}

	noRecoil(m_pNoRecoil->GetValue());
	noSpread(m_pNoSpread->GetValue());
	fireRate(m_pFireRate->GetValue());
}
