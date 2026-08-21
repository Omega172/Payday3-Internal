#pragma once
#include "pch.h"
#include "Types.hpp"

class Visuals : public BaseFeature
{
private:
	inline static uint8_t s_iVisualsPageId = ElementBase::AddPage("VISUALS_BUTTON"Hashed, ICON_FA_EYE);

	std::unique_ptr<RadioButtonIcon> m_pMenuButton = std::make_unique<RadioButtonIcon>(
		std::string("VISUALS_BUTTON"),
		"VISUALS_BUTTON"Hashed,
		ElementBase::Style_t({ .vec2Size = ImVec2(-0.1f, 0) }),
		ICON_FA_EYE,
		s_iVisualsPageId);

	std::unique_ptr<Page> m_pTab1Page = std::make_unique<Page>(
		"VISUALS_TAB1_PAGE",
		ElementBase::Style_t(),
		s_iVisualsPageId,
		0);

	std::unique_ptr<Group> m_pTab1Group = std::make_unique<Group>("VISUALS_TAB1_GROUP", ElementBase::Style_t{});

	std::unique_ptr<GroupChild> m_pTab1Left = std::make_unique<GroupChild>(
		"VISUALS_TAB1_LEFT",
		"VISUALS_TAB1_LEFT"Hashed,
		ElementBase::Style_t(),
		ImGuiChildFlags_Border);

	std::unique_ptr<GroupChild> m_pTab1Right = std::make_unique<GroupChild>(
		"VISUALS_TAB1_RIGHT",
		"VISUALS_TAB1_RIGHT"Hashed,
		ElementBase::Style_t{ .eSameLine = ElementBase::ESameLine::Same },
		ImGuiChildFlags_Border);

	std::unique_ptr<GroupChild> m_pTab1Bottom = std::make_unique<GroupChild>(
		"VISUALS_TAB1_BOTTOM",
		"VISUALS_TAB1_BOTTOM"Hashed,
		ElementBase::Style_t(),
		ImGuiChildFlags_Border);

	std::unique_ptr<Checkbox> m_pBoundingBox = std::make_unique<Checkbox>("VISUALS_BOUNDING_BOX", "VISUALS_BOUNDING_BOX"Hashed);
	std::unique_ptr<ColorPicker> m_pBoundingBoxCopColor = std::make_unique<ColorPicker>("VISUALS_BOUNDING_BOX_COP_COLOR", "VISUALS_BOUNDING_BOX_COP_COLOR"Hashed);
	std::unique_ptr<ColorPicker> m_pBoundingBoxCivilianColor = std::make_unique<ColorPicker>("VISUALS_BOUNDING_BOX_CIVILIAN_COLOR", "VISUALS_BOUNDING_BOX_CIVILIAN_COLOR"Hashed);

	std::unique_ptr<Checkbox> m_pName = std::make_unique<Checkbox>("VISUALS_NAME", "VISUALS_NAME"Hashed);
	std::unique_ptr<ColorPicker> m_pNameCopColor = std::make_unique<ColorPicker>("VISUALS_NAME_COP_COLOR", "VISUALS_NAME_COP_COLOR"Hashed);
	std::unique_ptr<ColorPicker> m_pNameCivilianColor = std::make_unique<ColorPicker>("VISUALS_NAME_CIVILIAN_COLOR", "VISUALS_NAME_CIVILIAN_COLOR"Hashed);

	std::unique_ptr<Checkbox> m_pDistance = std::make_unique<Checkbox>("VISUALS_DISTANCE", "VISUALS_DISTANCE"Hashed);
	std::unique_ptr<ColorPicker> m_pDistanceCopColor = std::make_unique<ColorPicker>("VISUALS_DISTANCE_COP_COLOR", "VISUALS_DISTANCE_COP_COLOR"Hashed);
	std::unique_ptr<ColorPicker> m_pDistanceCivilianColor = std::make_unique<ColorPicker>("VISUALS_DISTANCE_CIVILIAN_COLOR", "VISUALS_DISTANCE_CIVILIAN_COLOR"Hashed);

	std::unique_ptr<Checkbox> m_pHealthBar = std::make_unique<Checkbox>("VISUALS_HEALTH_BAR", "VISUALS_HEALTH_BAR"Hashed);
	std::unique_ptr<ColorPicker> m_pHealthBarCopColor = std::make_unique<ColorPicker>("VISUALS_HEALTH_BAR_COP_COLOR", "VISUALS_HEALTH_BAR_COP_COLOR"Hashed);
	std::unique_ptr<ColorPicker> m_pHealthBarCivilianColor = std::make_unique<ColorPicker>("VISUALS_HEALTH_BAR_CIVILIAN_COLOR", "VISUALS_HEALTH_BAR_CIVILIAN_COLOR"Hashed);

	std::unique_ptr<Checkbox> m_pArmorBar = std::make_unique<Checkbox>("VISUALS_ARMOR_BAR", "VISUALS_ARMOR_BAR"Hashed);
	std::unique_ptr<ColorPicker> m_pArmorBarColor = std::make_unique<ColorPicker>("VISUALS_ARMOR_BAR_COLOR", "VISUALS_ARMOR_BAR_COLOR"Hashed);

	std::unique_ptr<Checkbox> m_pSkeleton = std::make_unique<Checkbox>("VISUALS_SKELETON", "VISUALS_SKELETON"Hashed);
	std::unique_ptr<ColorPicker> m_pSkeletonCopColor = std::make_unique<ColorPicker>("VISUALS_SKELETON_COP_COLOR", "VISUALS_SKELETON_COP_COLOR"Hashed);
	std::unique_ptr<ColorPicker> m_pSkeletonCivilianColor = std::make_unique<ColorPicker>("VISUALS_SKELETON_CIVILIAN_COLOR", "VISUALS_SKELETON_CIVILIAN_COLOR"Hashed);

	std::unique_ptr<Checkbox> m_pHighlight = std::make_unique<Checkbox>("VISUALS_HIGHLIGHT", "VISUALS_HIGHLIGHT"Hashed);

	std::unique_ptr<Checkbox> m_pItem = std::make_unique<Checkbox>("VISUALS_ITEM", "VISUALS_ITEM"Hashed);
	std::unique_ptr<ColorPicker> m_pItemCashColor = std::make_unique<ColorPicker>("VISUALS_ITEM_CASH_COLOR", "VISUALS_ITEM_CASH_COLOR"Hashed);
	std::unique_ptr<ColorPicker> m_pItemDepositBoxColor = std::make_unique<ColorPicker>("VISUALS_ITEM_DEPOSITBOX_COLOR", "VISUALS_ITEM_DEPOSITBOX_COLOR"Hashed);
	std::unique_ptr<ColorPicker> m_pItemKeycardColor = std::make_unique<ColorPicker>("VISUALS_ITEM_KEYCARD_COLOR", "VISUALS_ITEM_KEYCARD_COLOR"Hashed);

	std::unique_ptr<MultiSelectCombo> m_pFilters = std::make_unique<MultiSelectCombo>("VISUALS_FILTERS", "VISUALS_FILTERS"Hashed, ElementBase::Style_t{ .vec2Size = ImVec2(-0.1f, 0) });
	std::unique_ptr<MultiSelectCombo> m_pItemFilters = std::make_unique<MultiSelectCombo>("VISUALS_ITEM_FILTERS", "VISUALS_ITEM_FILTERS"Hashed, ElementBase::Style_t{ .vec2Size = ImVec2(-0.1f, 0) });

	std::vector<Types::ESPData> m_vESPData;
	std::unordered_map<SDK::USkeletalMeshComponent*, Types::BoneCache> m_BoneCache;
	std::unordered_map<SDK::UClass*, Types::EnemyType> m_ClassCache;
	std::vector<Types::ItemData> m_vItemData;
	std::unordered_map<SDK::UClass*, Types::ItemType> m_ItemTypeCache;

	struct FrameSettings
	{
		bool DrawBox = false;
		bool DrawName = false;
		bool DrawDistance = false;
		bool DrawHealthBar = false;
		bool DrawArmorBar = false;
		bool DrawSkeleton = false;
		bool DrawHighlight = false;
		bool DrawItems = false;
		bool ShowCops = false;
		bool ShowCivilians = false;
		bool ShowCash = false;
		bool ShowDepositBox = false;
		bool ShowKeycards = false;
	};

	void CollectFrameData(SDK::UWorld* pGWorld, SDK::APlayerController* pPlayerController, SDK::APlayerCameraManager* pCameraManager, SDK::AActor* pLocalPlayer, const FrameSettings& settings);
	Types::ItemType GetItemType(SDK::AActor* actor);
	Types::EnemyType GetEnemyType(SDK::AActor* actor);

public:
	void UpdateMenuVisibility();
	void HandleMenu() override;
	void Render() override;
	void Run() override;
	RadioButtonIcon* GetMenuButton() const { return m_pMenuButton.get(); }
	std::string GetName() override { return "Visuals"; };
};

inline std::unique_ptr<Visuals> pVisuals = std::make_unique<Visuals>();
