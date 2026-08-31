#pragma once
#include "pch.h"

struct WeaponDataBackupEntry_t
{
	float m_flViewSpeedDeflect;
	float m_flGunKickBackSpeedDeflect;
	float m_flGunSpeedDeflect;

	float m_flInnerClusterSpreadMultiplier;
	float m_flFireSpreadStart;
	float m_flFireSpreadMinCap;
	float m_flFireSpreadCap;
	float m_flFireSpreadIncrease;

	uint32_t m_iProjectilesPerFiredRound;
	float m_flRoundsPerMinute;
	SDK::ESBZFireMode m_eFireMode;
};

struct AttrDataBackupEntry_t
{
	SDK::FGameplayAttributeData m_flHealth;
	SDK::FGameplayAttributeData m_flStamina;

	SDK::FGameplayAttributeData m_flPrimaryEquippableAmmoInventory;
	SDK::FGameplayAttributeData m_flSecondaryEquippableAmmoInventory;
	SDK::FGameplayAttributeData m_flTertiaryEquippableAmmoInventory;

	SDK::FGameplayAttributeData m_flPrimaryThrowableAmmoInventory;
	SDK::FGameplayAttributeData m_flSecondaryThrowableAmmoInventory;
	SDK::FGameplayAttributeData m_flTertiaryThrowableAmmoInventory;

	SDK::FGameplayAttributeData m_flPrimaryToolAmmoInventory;
	SDK::FGameplayAttributeData m_flSecondaryToolAmmoInventory;
	SDK::FGameplayAttributeData m_flTertiaryToolAmmoInventory;
};

class Player : public BaseFeature
{
private:
	inline static uint8_t s_iPlayerPageId = ElementBase::AddPage("PLAYER_BUTTON"Hashed, ICON_FA_USER);

	std::unique_ptr<RadioButtonIcon> m_pMenuButton = std::make_unique<RadioButtonIcon>(
		std::string("PLAYER_BUTTON"),
		"PLAYER_BUTTON"Hashed,
		ElementBase::Style_t({ .vec2Size = ImVec2(-0.1f, 0) }),
		ICON_FA_USER,
		s_iPlayerPageId);

	std::unique_ptr<Page> m_pTab1Page = std::make_unique<Page>("PLAYER_TAB1_PAGE", ElementBase::Style_t(), s_iPlayerPageId, 0);
	std::unique_ptr<Page> m_pTab2Page = std::make_unique<Page>("PLAYER_TAB2_PAGE", ElementBase::Style_t(), s_iPlayerPageId, 1);
	std::unique_ptr<Page> m_pTab3Page = std::make_unique<Page>("PLAYER_TAB3_PAGE", ElementBase::Style_t(), s_iPlayerPageId, 2);

	std::unique_ptr<Group> m_pTab1Group = std::make_unique<Group>("PLAYER_TAB1_GROUP", ElementBase::Style_t{});
	std::unique_ptr<Group> m_pTab2Group = std::make_unique<Group>("PLAYER_TAB2_GROUP", ElementBase::Style_t{});
	std::unique_ptr<Group> m_pTab3Group = std::make_unique<Group>("PLAYER_TAB3_GROUP", ElementBase::Style_t{});

	std::unique_ptr<GroupChild> m_pTab1Left = std::make_unique<GroupChild>(
		"PLAYER_TAB1_LEFT", "PLAYER_TAB1_LEFT"Hashed, ElementBase::Style_t(), ImGuiChildFlags_Border);
	std::unique_ptr<GroupChild> m_pTab1Right = std::make_unique<GroupChild>(
		"PLAYER_TAB1_RIGHT", "PLAYER_TAB1_RIGHT"Hashed, ElementBase::Style_t{ .eSameLine = ElementBase::ESameLine::Same }, ImGuiChildFlags_Border);

	std::unique_ptr<GroupChild> m_pTab2Left = std::make_unique<GroupChild>(
		"PLAYER_TAB2_LEFT", "PLAYER_TAB2_LEFT"Hashed, ElementBase::Style_t(), ImGuiChildFlags_Border);

	std::unique_ptr<GroupChild> m_pTab3Left = std::make_unique<GroupChild>(
		"PLAYER_TAB3_LEFT", "PLAYER_TAB3_LEFT"Hashed, ElementBase::Style_t(), ImGuiChildFlags_Border);
	std::unique_ptr<GroupChild> m_pTab3Right = std::make_unique<GroupChild>(
		"PLAYER_TAB3_RIGHT", "PLAYER_TAB3_RIGHT"Hashed, ElementBase::Style_t{ .eSameLine = ElementBase::ESameLine::Same }, ImGuiChildFlags_Border);

	// Tab 1
	std::unique_ptr<Combo> m_pGodModeType = std::make_unique<Combo>("PLAYER_GODMODE_TYPE", "PLAYER_GODMODE_TYPE"Hashed, ElementBase::Style_t{ .iFlags = ImGuiComboFlags_WidthFitPreview });
	std::unique_ptr<Checkbox> m_pGodMode = std::make_unique<Checkbox>("PLAYER_GODMODE", "PLAYER_GODMODE"Hashed);
	std::unique_ptr<Checkbox> m_pInfStamina = std::make_unique<Checkbox>("PLAYER_INF_STAMINA", "PLAYER_INF_STAMINA"Hashed);
	std::unique_ptr<Checkbox> m_pInstaMelee = std::make_unique<Checkbox>("PLAYER_INSTA_MELEE", "PLAYER_INSTA_MELEE"Hashed);
	std::unique_ptr<Checkbox> m_pNoScreenshake = std::make_unique<Checkbox>("PLAYER_NO_SCREENSHAKE", "PLAYER_NO_SCREENSHAKE"Hashed);
	std::unique_ptr<Checkbox> m_pNoFallDamage = std::make_unique<Checkbox>("PLAYER_NO_FALLDAMAGE", "PLAYER_NO_FALLDAMAGE"Hashed);
	std::unique_ptr<Checkbox> m_pNoDetection = std::make_unique<Checkbox>("PLAYER_NO_DETECTION", "PLAYER_NO_DETECTION"Hashed);

	// Tab 2 - population logic isn't implemented, this mirrors the placeholder rows from V2
	std::unique_ptr<Table> m_pPlayerTable = std::make_unique<Table>("PLAYER_TABLE", 4);

	// Tab 3
	std::unique_ptr<Checkbox> m_pInstaReload = std::make_unique<Checkbox>("PLAYER_INSTA_RELOAD", "PLAYER_INSTA_RELOAD"Hashed);
	std::unique_ptr<Checkbox> m_pInfAmmo = std::make_unique<Checkbox>("PLAYER_INF_AMMO", "PLAYER_INF_AMMO"Hashed);
	std::unique_ptr<Checkbox> m_pNoRecoil = std::make_unique<Checkbox>("PLAYER_NO_RECOIL", "PLAYER_NO_RECOIL"Hashed);
	std::unique_ptr<Checkbox> m_pNoSpread = std::make_unique<Checkbox>("PLAYER_NO_SPREAD", "PLAYER_NO_SPREAD"Hashed);
	std::unique_ptr<SliderInt> m_pFireRateSlider = std::make_unique<SliderInt>("PLAYER_FIRE_RATE_SLIDER", "PLAYER_FIRE_RATE_SLIDER"Hashed, ElementBase::Style_t{ .vec2Size = ImVec2(100.f, 0.f), .iFlags = ImGuiComboFlags_WidthFitPreview }, 100, 0, 1000);
	std::unique_ptr<Checkbox> m_pFireRate = std::make_unique<Checkbox>("PLAYER_FIRE_RATE", "PLAYER_FIRE_RATE"Hashed);

	void blockDamage(bool bEnabled);
	bool m_bBlockDamageApplied = false;
	bool m_bOriginalCanBeDamaged = true;

	bool m_bInstantMeleeApplied = false;
	float m_flOriginalMeleeRate = 1.0f;
	SDK::UAnimMontage* m_pLastMeleeMontage = nullptr;
	void InstantMelee(bool bEnabled);

	bool m_bInstantReloadApplied = false;
	float m_flOriginalReloadRate = 1.0f;
	SDK::UAnimMontage* m_pLastReloadMontage = nullptr;
	void InstantReload(bool bEnabled);

	void noRecoil(bool bEnabled);
	void noSpread(bool bEnabled);
	void wallbang(bool bEnabled);
	void fireRate(bool bEnabled);
	void ApplyWallbangToFireData(SDK::USBZWeaponFireData* pFire, bool bEnabled, const WeaponDataBackupEntry_t* backup);

	bool g_bDidBackupWeaponData;
	WeaponDataBackupEntry_t* GetWeaponBackupData(SDK::USBZRangedWeaponData* pWeaponData);
	std::vector<SDK::USBZRangedWeaponData*> GetCurrentWeaponData(SDK::ASBZPlayerCharacter* pLocalChar);

	bool g_bDidBackupAttrData;
	AttrDataBackupEntry_t* GetAttrBackupData(SDK::ASBZPlayerCharacter* pLocalChar);

	// ElementInput has no value-changed callback, so the off-transition restores below (which
	// must fire exactly once, not every frame) are edge-detected here instead.
	bool m_bPrevGodMode = false;
	int m_iPrevGodModeType = 0;
	bool m_bPrevInfAmmo = false;

public:
	void HandleMenu() override;
	void Run() override;
	RadioButtonIcon* GetMenuButton() const { return m_pMenuButton.get(); }
	std::string GetName() override { return "Player"; };
};

static std::unordered_map<size_t, WeaponDataBackupEntry_t> g_mapWeaponDataBackup;
static std::unordered_map<size_t, AttrDataBackupEntry_t> g_attrDataBackup;
inline std::unique_ptr<Player> pPlayer = std::make_unique<Player>();
