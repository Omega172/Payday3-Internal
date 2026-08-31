#pragma once
#include "pch.h"

class Aimbot : public BaseFeature
{
private:
	inline static uint8_t s_iAimbotPageId = ElementBase::AddPage("AIMBOT_BUTTON"Hashed, ICON_FA_CROSSHAIRS);

	std::unique_ptr<RadioButtonIcon> m_pMenuButton = std::make_unique<RadioButtonIcon>(
		std::string("AIMBOT_BUTTON"),
		"AIMBOT_BUTTON"Hashed,
		ElementBase::Style_t({ .vec2Size = ImVec2(-0.1f, 0) }),
		ICON_FA_CROSSHAIRS,
		s_iAimbotPageId);

	std::unique_ptr<Page> m_pTab1Page = std::make_unique<Page>(
		"AIMBOT_TAB1_PAGE",
		ElementBase::Style_t(),
		s_iAimbotPageId,
		0);

	std::unique_ptr<Group> m_pTab1Group = std::make_unique<Group>("AIMBOT_TAB1_GROUP", ElementBase::Style_t{});

	std::unique_ptr<GroupChild> m_pTab1Left = std::make_unique<GroupChild>(
		"AIMBOT_TAB1_LEFT",
		"AIMBOT_TAB1_LEFT"Hashed,
		ElementBase::Style_t(),
		ImGuiChildFlags_Border);

	std::unique_ptr<GroupChild> m_pTab1Right = std::make_unique<GroupChild>(
		"AIMBOT_TAB1_RIGHT",
		"AIMBOT_TAB1_RIGHT"Hashed,
		ElementBase::Style_t{ .eSameLine = ElementBase::ESameLine::Same },
		ImGuiChildFlags_Border);

	std::unique_ptr<Checkbox> m_pAimbotEnabled = std::make_unique<Checkbox>("AIMBOT_ENABLED", "AIMBOT_ENABLED"Hashed);
	std::unique_ptr<Checkbox> m_pAimbotVisibleCheck = std::make_unique<Checkbox>("AIMBOT_VISIBLE_CHECK", "AIMBOT_VISIBLE_CHECK"Hashed);
	std::unique_ptr<Checkbox> m_pAimbotFOVEnabled = std::make_unique<Checkbox>("AIMBOT_FOV_ENABLED", "AIMBOT_FOV_ENABLED"Hashed);
	std::unique_ptr<Hotkey> m_pAimbotHotkey = std::make_unique<Hotkey>("AIMBOT_HOTKEY", "AIMBOT_HOTKEY"Hashed);
	std::unique_ptr<Combo> m_pAimbotType = std::make_unique<Combo>("AIMBOT_TYPE", "AIMBOT_TYPE"Hashed, ElementBase::Style_t{ .iFlags = ImGuiComboFlags_WidthFitPreview });
	std::unique_ptr<Combo> m_pAimbotTarget = std::make_unique<Combo>("AIMBOT_TARGET", "AIMBOT_TARGET"Hashed, ElementBase::Style_t{ .iFlags = ImGuiComboFlags_WidthFitPreview });
	std::unique_ptr<ColorPicker> m_pAimbotFOVColor = std::make_unique<ColorPicker>("AIMBOT_FOV_COLOR", "AIMBOT_FOV_COLOR"Hashed);
	std::unique_ptr<SliderInt> m_pFOV = std::make_unique<SliderInt>("AIMBOT_FOV", "AIMBOT_FOV"Hashed, ElementBase::Style_t{ .vec2Size = ImVec2(100.f, 0.f), .iFlags = ImGuiComboFlags_WidthFitPreview }, 60, 0, 200);
	std::unique_ptr<SliderInt> m_pSmoothing = std::make_unique<SliderInt>("AIMBOT_SMOOTHING", "AIMBOT_SMOOTHING"Hashed, ElementBase::Style_t{ .vec2Size = ImVec2(100.f, 0.f), .iFlags = ImGuiComboFlags_WidthFitPreview }, 10, 0, 100);

public:
	void HandleMenu() override;
	void Render() override;
	void Run() override;
	void UpdateAim();
	int CurrentAimbotType();
	RadioButtonIcon* GetMenuButton() const { return m_pMenuButton.get(); }
	std::string GetName() override { return "Aimbot"; };

	bool ShouldOverrideView = false;
	SDK::FVector OverrideLocation{};
	SDK::FRotator OverrideRotation{};
};

inline std::unique_ptr<Aimbot> pAimbot = std::make_unique<Aimbot>();
