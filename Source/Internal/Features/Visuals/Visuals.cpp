#include "pch.h"
#include "Visuals.hpp"
#include "VisualsHelpers.hpp"
#include "../Shared/Helpers.hpp"
#include <vector>
#include <string>

#undef min
#undef max

namespace
{
	std::string FormatEntityName(const std::string& name)
	{
		if (name.empty())
			return "Unknown";

		const char* rawName = name.c_str();
		size_t length = strlen(rawName);
		if (length == 0 || length >= 256)
			return "Unknown";

		for (size_t i = 0; i < length; ++i)
		{
			if (rawName[i] < 32 || rawName[i] > 126)
				return "Invalid";
		}

		return name;
	}

	std::string FormatDistanceText(float distance)
	{
		if (distance >= 0 && distance < 10000)
		{
			char buffer[32];
			snprintf(buffer, sizeof(buffer), "[%.0fm]", distance);
			return buffer;
		}

		return "[???m]";
	}

	bool TryReadPawnStats(SDK::ASBZCharacter* character, float& health, float& healthMax, float& armor, float& armorMax)
	{
		auto* abilitySystem = character->AbilitySystem;
		if (!abilitySystem)
			return false;

		UC::TArray<SDK::UAttributeSet*>* spawnedAttrs = (UC::TArray<SDK::UAttributeSet*>*)((uintptr_t)abilitySystem + 0x0150);
		if (!spawnedAttrs)
			return false;

		for (int i = 0; i < spawnedAttrs->Num(); ++i)
		{
			auto* attrSet = (*spawnedAttrs)[i];
			if (attrSet && attrSet->IsA(SDK::USBZPawnAttributeSet::StaticClass()))
			{
				auto* pawnAttrs = static_cast<SDK::USBZPawnAttributeSet*>(attrSet);
				health = pawnAttrs->Health.CurrentValue;
				healthMax = pawnAttrs->HealthMax.CurrentValue;
				armor = pawnAttrs->Armor.CurrentValue;
				armorMax = pawnAttrs->ArmorMax.CurrentValue;
				return true;
			}
		}

		return false;
	}
}

Types::EnemyType Visuals::GetEnemyType(SDK::AActor* actor)
{
	return Helpers::ResolveEnemyType(m_ClassCache, actor);
}

Types::ItemType Visuals::GetItemType(SDK::AActor* actor)
{
	return Helpers::ResolveItemType(m_ItemTypeCache, actor);
}

void Visuals::CollectFrameData(SDK::UWorld* pGWorld, SDK::APlayerController* pPlayerController, SDK::APlayerCameraManager* pCameraManager, SDK::AActor* pLocalPlayer, const FrameSettings& settings)
{
	m_vESPData.clear();
	m_vESPData.reserve(256);
	m_vItemData.clear();

	const bool needsEnemyBoxData = settings.DrawBox || settings.DrawName || settings.DrawDistance || settings.DrawHealthBar || settings.DrawArmorBar;
	const bool needsEnemyStats = settings.DrawHealthBar || settings.DrawArmorBar;
	const bool needsItems = settings.DrawItems;

	SDK::FVector vecCameraLocation = pCameraManager->GetCameraLocation();
	SDK::USBZWorldRuntime* pWorldRuntime = SDK::USBZWorldRuntime::Get(pGWorld);
	if (!pWorldRuntime)
		return;

	UC::TArray<SDK::UObject*>& actors = pWorldRuntime->AllPawns->Objects;
	UC::TArray<SDK::ULevel*> vecLevels = pGWorld->Levels;

	for (int i = 0; i < actors.Num(); ++i)
	{
		if (!actors.IsValidIndex(i))
			break;

		auto pActor = reinterpret_cast<SDK::AActor*>(actors[i]);
		if (!pActor || pActor == pLocalPlayer)
			continue;

		auto pCharacter = reinterpret_cast<SDK::ASBZCharacter*>(pActor);
		if (!pCharacter || !pCharacter->bIsAlive || !pCharacter->Mesh)
			continue;

		Types::EnemyType type = GetEnemyType(pActor);
		const Types::EnemyInfo& info = Types::g_EnemyInfo[static_cast<size_t>(type)];
		const bool bIsCop = info.Category == Types::EnemyCategory::Cop;
		const bool bIsCivilian = info.Category == Types::EnemyCategory::Civilian;

		if (!bIsCop && !bIsCivilian)
			continue;
		if (bIsCop && !settings.ShowCops)
			continue;
		if (bIsCivilian && !settings.ShowCivilians)
			continue;

		ImVec4 screenBox{};
		if (needsEnemyBoxData)
		{
			auto optScreenBox = VisualsHelpers::CalculateScreenBoxForCharacter(pCharacter->Mesh, pPlayerController, pActor);
			if (!optScreenBox.has_value())
				continue;

			screenBox = optScreenBox.value();
		}

		float flHealth = 0.0f;
		float flHealthMax = 0.0f;
		float flArmor = 0.0f;
		float flArmorMax = 0.0f;
		if (needsEnemyStats)
			TryReadPawnStats(pCharacter, flHealth, flHealthMax, flArmor, flArmorMax);

		float flDistance = 0.0f;
		if (settings.DrawDistance)
			flDistance = (pActor->K2_GetActorLocation() - vecCameraLocation).Magnitude() / 100.0f;

		m_vESPData.push_back({
			screenBox,
			std::string(info.Name),
			pCharacter->Mesh,
			pCharacter,
			flHealth,
			flHealthMax,
			flArmor,
			flArmorMax,
			flDistance,
			bIsCop,
			bIsCivilian
		});

		if (settings.DrawSkeleton)
		{
			auto [cacheIt, inserted] = m_BoneCache.try_emplace(pCharacter->Mesh);
			if (!cacheIt->second.Initialized)
				VisualsHelpers::BuildBoneCache(pCharacter->Mesh, cacheIt->second);
		}
	}

	if (!needsItems)
		return;

	for (SDK::ULevel* pLevel : vecLevels)
	{
		if (!pLevel || !pLevel->Actors)
			continue;

		for (SDK::AActor* pActor : pLevel->Actors)
		{
			if (!pActor)
				continue;

			Types::ItemType type = GetItemType(pActor);
			if (type == Types::ItemType::None)
				continue;

			if (!((type == Types::ItemType::Cash && settings.ShowCash) || (type == Types::ItemType::DepositBox && settings.ShowDepositBox) || (type == Types::ItemType::Keycard && settings.ShowKeycards)))
				continue;

			m_vItemData.push_back({
				SDK::FVector2D{},
				pActor->K2_GetActorLocation(),
				std::string(pActor->GetName()),
				type
			});
		}
	}
}

void Visuals::UpdateMenuVisibility()
{
	const bool copSelected = m_pFilters->IsSelected(0);
	const bool civilianSelected = m_pFilters->IsSelected(1);

	const bool cashSelected = m_pItemFilters->IsSelected(0);
	const bool depositBoxSelected = m_pItemFilters->IsSelected(1);
	const bool keycardSelected = m_pItemFilters->IsSelected(2);

	m_pBoundingBoxCopColor->SetVisible(m_pBoundingBox->GetValue() && copSelected);
	m_pBoundingBoxCivilianColor->SetVisible(m_pBoundingBox->GetValue() && civilianSelected);

	m_pNameCopColor->SetVisible(m_pName->GetValue() && copSelected);
	m_pNameCivilianColor->SetVisible(m_pName->GetValue() && civilianSelected);

	m_pDistanceCopColor->SetVisible(m_pDistance->GetValue() && copSelected);
	m_pDistanceCivilianColor->SetVisible(m_pDistance->GetValue() && civilianSelected);

	m_pHealthBarCopColor->SetVisible(m_pHealthBar->GetValue() && copSelected);
	m_pHealthBarCivilianColor->SetVisible(m_pHealthBar->GetValue() && civilianSelected);

	m_pArmorBarColor->SetVisible(m_pArmorBar->GetValue() && copSelected);

	m_pSkeletonCopColor->SetVisible(m_pSkeleton->GetValue() && copSelected);
	m_pSkeletonCivilianColor->SetVisible(m_pSkeleton->GetValue() && civilianSelected);

	m_pItemCashColor->SetVisible(m_pItem->GetValue() && cashSelected);
	m_pItemDepositBoxColor->SetVisible(m_pItem->GetValue() && depositBoxSelected);
	m_pItemKeycardColor->SetVisible(m_pItem->GetValue() && keycardSelected);
}

void Visuals::HandleMenu()
{
	static std::once_flag onceflag;
	std::call_once(onceflag, [this]() {
		auto pHeaderGroup = static_cast<HeaderGroup*>(Framework::menu->GetChild("HEADER_GROUP"));
		if (pHeaderGroup)
			pHeaderGroup->AddHeaders(Visuals::s_iVisualsPageId, { "VISUALS_TAB1"Hashed });

		m_pTab1Left->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() + 60.0f + 30.0f) / 2);
		});
		m_pTab1Right->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() + 60.0f + 30.0f) / 2);
		});
		m_pTab1Bottom->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f * 2), (ImGui::GetWindowHeight() - 40.0f - 60.0f * 2) / 2);
		});

		m_pTab1Left->AddElement(m_pBoundingBox.get());
		m_pTab1Right->AddElement(m_pBoundingBoxCopColor.get());
		m_pTab1Right->AddElement(m_pBoundingBoxCivilianColor.get());
		m_pBoundingBoxCopColor->SetValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
		m_pBoundingBoxCivilianColor->SetValue(ImVec4(0.0f, 1.0f, 0.0f, 1.0f));

		m_pTab1Left->AddElement(m_pName.get());
		m_pTab1Right->AddElement(m_pNameCopColor.get());
		m_pTab1Right->AddElement(m_pNameCivilianColor.get());
		m_pNameCopColor->SetValue(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		m_pNameCivilianColor->SetValue(ImVec4(0.0f, 0.0f, 1.0f, 1.0f));

		m_pTab1Left->AddElement(m_pDistance.get());
		m_pTab1Right->AddElement(m_pDistanceCopColor.get());
		m_pTab1Right->AddElement(m_pDistanceCivilianColor.get());
		m_pDistanceCopColor->SetValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
		m_pDistanceCivilianColor->SetValue(ImVec4(0.0f, 1.0f, 0.0f, 1.0f));

		m_pTab1Left->AddElement(m_pHealthBar.get());
		m_pTab1Right->AddElement(m_pHealthBarCopColor.get());
		m_pTab1Right->AddElement(m_pHealthBarCivilianColor.get());
		m_pHealthBarCopColor->SetValue(ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
		m_pHealthBarCivilianColor->SetValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));

		m_pTab1Left->AddElement(m_pArmorBar.get());
		m_pTab1Right->AddElement(m_pArmorBarColor.get());
		m_pArmorBarColor->SetValue(ImVec4(0.0f, 0.5f, 1.0f, 1.0f));

		m_pTab1Left->AddElement(m_pSkeleton.get());
		m_pTab1Right->AddElement(m_pSkeletonCopColor.get());
		m_pTab1Right->AddElement(m_pSkeletonCivilianColor.get());
		m_pSkeletonCopColor->SetValue(ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
		m_pSkeletonCivilianColor->SetValue(ImVec4(0.0f, 1.0f, 1.0f, 1.0f));

		m_pTab1Left->AddElement(m_pHighlight.get());

		m_pTab1Left->AddElement(m_pItem.get());
		m_pTab1Right->AddElement(m_pItemCashColor.get());
		m_pTab1Right->AddElement(m_pItemDepositBoxColor.get());
		m_pTab1Right->AddElement(m_pItemKeycardColor.get());
		m_pItemCashColor->SetValue(ImVec4(0.0f, 1.0f, 1.0f, 1.0f));
		m_pItemDepositBoxColor->SetValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
		m_pItemKeycardColor->SetValue(ImVec4(0.0f, 0.0f, 1.0f, 1.0f));

		// Filters
        m_pFilters->AddOption("COP", [](bool bEnabled) {});
        m_pFilters->AddOption("CIVILIAN", [](bool bEnabled) {});
        m_pItemFilters->AddOption("CASH", [](bool bEnabled) {});
        m_pItemFilters->AddOption("DEPOSIT BOX", [](bool bEnabled) {});
        m_pItemFilters->AddOption("KEYCARDS", [](bool bEnabled) {});

        m_pTab1Bottom->AddElement(m_pFilters.get());
		m_pTab1Bottom->AddElement(m_pItemFilters.get());

		m_pTab1Group->AddElement(m_pTab1Left.get());
		m_pTab1Group->AddElement(m_pTab1Right.get());
		m_pTab1Group->AddElement(m_pTab1Bottom.get());
		m_pTab1Page->AddElement(m_pTab1Group.get());

		Framework::menu->GetChild("HEADER_GROUP")->GetChild("BODY")->AddElement(m_pTab1Page.get());
	});

	UpdateMenuVisibility();
}

void Visuals::Render()
{
	const bool drawBox = m_pBoundingBox->GetValue();
	const bool drawName = m_pName->GetValue();
	const bool drawDistance = m_pDistance->GetValue();
	const bool drawHealthBar = m_pHealthBar->GetValue();
	const bool drawArmorBar = m_pArmorBar->GetValue();
	const bool drawSkeleton = m_pSkeleton->GetValue();
	const bool drawHighlight = m_pHighlight->GetValue();
	const bool drawItems = m_pItem->GetValue();

	const ImU32 boxCopColor = ImGui::ColorConvertFloat4ToU32(m_pBoundingBoxCopColor->GetValue());
	const ImU32 boxCivilianColor = ImGui::ColorConvertFloat4ToU32(m_pBoundingBoxCivilianColor->GetValue());

	const ImU32 nameCopColor = ImGui::ColorConvertFloat4ToU32(m_pNameCopColor->GetValue());
	const ImU32 nameCivilianColor = ImGui::ColorConvertFloat4ToU32(m_pNameCivilianColor->GetValue());

	const ImU32 distanceCopColor = ImGui::ColorConvertFloat4ToU32(m_pDistanceCopColor->GetValue());
	const ImU32 distanceCivilianColor = ImGui::ColorConvertFloat4ToU32(m_pDistanceCivilianColor->GetValue());

	const ImU32 healthBarCopColor = ImGui::ColorConvertFloat4ToU32(m_pHealthBarCopColor->GetValue());
	const ImU32 healthBarCivilianColor = ImGui::ColorConvertFloat4ToU32(m_pHealthBarCivilianColor->GetValue());

	const ImU32 armorBarColor = ImGui::ColorConvertFloat4ToU32(m_pArmorBarColor->GetValue());

	const ImU32 skeletonCopColor = ImGui::ColorConvertFloat4ToU32(m_pSkeletonCopColor->GetValue());
	const ImU32 skeletonCivilianColor = ImGui::ColorConvertFloat4ToU32(m_pSkeletonCivilianColor->GetValue());

	const ImU32 itemCashColor = ImGui::ColorConvertFloat4ToU32(m_pItemCashColor->GetValue());
	const ImU32 itemDepositBoxColor = ImGui::ColorConvertFloat4ToU32(m_pItemDepositBoxColor->GetValue());
	const ImU32 itemKeycardColor = ImGui::ColorConvertFloat4ToU32(m_pItemKeycardColor->GetValue());

	if (!drawBox && !drawName && !drawDistance && !drawHealthBar && !drawArmorBar && !drawSkeleton && !drawHighlight && !drawItems)
		return;

	SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
	if (!pGWorld || !pGWorld->PersistentLevel)
		return;

	SDK::APlayerController* pPlayerController = SDK::UGameplayStatics::GetPlayerController(pGWorld, 0);
	if (!pPlayerController || !pPlayerController->PlayerCameraManager)
		return;

	SDK::APlayerCameraManager* pCameraManager = pPlayerController->PlayerCameraManager;

	CollectFrameData(pGWorld, pPlayerController, pCameraManager, pPlayerController->AcknowledgedPawn, {
		drawBox,
		drawName,
		drawDistance,
		drawHealthBar,
		drawArmorBar,
		drawSkeleton,
		drawHighlight,
		drawItems,
		m_pFilters->IsSelected(0),
        m_pFilters->IsSelected(1),
        m_pItemFilters->IsSelected(0),
        m_pItemFilters->IsSelected(1),
        m_pItemFilters->IsSelected(2)
	});

	if (m_vESPData.empty() && m_vItemData.empty())
		return;

	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();
	if (!pDrawList)
		return;

	for (const auto& entity : m_vESPData)
	{
		if (drawBox)
		{
			const ImU32 color = entity.IsCop ? boxCopColor : boxCivilianColor;

			pDrawList->AddRect(
				ImVec2(entity.Box.x - 1.0f, entity.Box.y - 1.0f),
				ImVec2(entity.Box.z + 1.0f, entity.Box.w + 1.0f),
				IM_COL32(0, 0, 0, 255), 0.0f, 0, 1.6f
			);

			pDrawList->AddRect(
				ImVec2(entity.Box.x, entity.Box.y),
				ImVec2(entity.Box.z, entity.Box.w),
				color, 0.0f, 0, 1.5f
			);
		}

		if (drawName || drawDistance)
		{
			std::string nameText = drawName ? FormatEntityName(entity.Name) : std::string();
			std::string distanceText = drawDistance ? FormatDistanceText(entity.Distance) : std::string();

			if (nameText.empty() && distanceText.empty())
				continue;

			ImVec2 nameSize = ImGui::CalcTextSize(nameText.c_str());
			ImVec2 distanceSize = ImGui::CalcTextSize(distanceText.c_str());

			if (!std::isfinite(nameSize.x) || !std::isfinite(nameSize.y) ||
				!std::isfinite(distanceSize.x) || !std::isfinite(distanceSize.y))
				continue;

			float totalWidth = nameSize.x;
			if (!nameText.empty() && !distanceText.empty())
				totalWidth += ImGui::CalcTextSize(" ").x;
			totalWidth += distanceSize.x;

			float boxWidth = entity.Box.z - entity.Box.x;
			float boxHeight = entity.Box.w - entity.Box.y;

			if (boxWidth <= 0 || boxHeight <= 0)
				continue;

			ImVec2 textPos(
				(boxWidth - totalWidth) / 2.f + entity.Box.x,
				entity.Box.y - nameSize.y - 4.f
			);

			if (!std::isfinite(textPos.x) || !std::isfinite(textPos.y))
				continue;

			if (!nameText.empty())
			{
				const ImU32 color = entity.IsCop ? nameCopColor : nameCivilianColor;

				pDrawList->AddText(ImVec2(textPos.x - 1, textPos.y - 1), IM_COL32(0, 0, 0, 255), nameText.c_str());
				pDrawList->AddText(textPos, color, nameText.c_str());
				textPos.x += nameSize.x;
			}

			if (!nameText.empty() && !distanceText.empty())
				textPos.x += ImGui::CalcTextSize(" ").x;

			if (!distanceText.empty())
			{
				const ImU32 color = entity.IsCop ? distanceCopColor : distanceCivilianColor;

				pDrawList->AddText(ImVec2(textPos.x - 1, textPos.y - 1), IM_COL32(0, 0, 0, 255), distanceText.c_str());
				pDrawList->AddText(textPos, color, distanceText.c_str());
			}
		}

		if (drawHealthBar)
		{
			const float barWidth = 4.0f;
			const float barHeight = entity.Box.w - entity.Box.y;

			if (barHeight > 0)
			{
				float healthPercent = entity.HealthMax > 0 ? entity.Health / entity.HealthMax : 0.0f;
				float healthBarHeight = barHeight * healthPercent;
				const ImU32 healthColor = entity.IsCop ? healthBarCopColor : healthBarCivilianColor;

				pDrawList->AddRectFilled(
					ImVec2(entity.Box.x - barWidth - 3, entity.Box.w - barHeight),
					ImVec2(entity.Box.x - 1, entity.Box.w),
					IM_COL32(0, 0, 0, 255)
				);

				pDrawList->AddRectFilled(
					ImVec2(entity.Box.x - barWidth - 2, entity.Box.w - healthBarHeight),
					ImVec2(entity.Box.x - 2, entity.Box.w),
					healthColor
				);
			}
		}

		if (drawArmorBar)
		{
			const float barWidth = 4.0f;
			const float barHeight = entity.Box.w - entity.Box.y;

			if (barHeight > 0)
			{
				float armorPercent = entity.ArmorMax > 0 ? entity.Armor / entity.ArmorMax : 0.0f;
				float armorBarHeight = barHeight * armorPercent;

				pDrawList->AddRectFilled(
					ImVec2(entity.Box.x - barWidth * 2 - 5, entity.Box.w - armorBarHeight),
					ImVec2(entity.Box.x - barWidth - 3, entity.Box.w),
					IM_COL32(0, 0, 0, 255)
				);

				pDrawList->AddRectFilled(
					ImVec2(entity.Box.x - barWidth * 2 - 4, entity.Box.w - armorBarHeight),
					ImVec2(entity.Box.x - barWidth - 4, entity.Box.w),
					armorBarColor
				);
			}
		}

		if (drawSkeleton)
		{
			Types::BoneCache& cache = m_BoneCache[entity.Mesh];
			ImU32 color = entity.IsCop ? skeletonCopColor : skeletonCivilianColor;

			if (!cache.Initialized)
				VisualsHelpers::BuildBoneCache(entity.Mesh, cache);

			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.Head, cache.Neck, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.Neck, cache.Spine3, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.Spine3, cache.Hips, color);

			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.Spine3, cache.LeftShoulder, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.LeftShoulder, cache.LeftUpperArm, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.LeftUpperArm, cache.LeftForeArm, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.LeftForeArm, cache.LeftHand, color);

			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.Spine3, cache.RightShoulder, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.RightShoulder, cache.RightUpperArm, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.RightUpperArm, cache.RightForeArm, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.RightForeArm, cache.RightHand, color);

			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.Hips, cache.LeftUpperLeg, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.LeftUpperLeg, cache.LeftLowerLeg, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.LeftLowerLeg, cache.LeftFoot, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.LeftFoot, cache.LeftToe, color);

			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.Hips, cache.RightUpperLeg, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.RightUpperLeg, cache.RightLowerLeg, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.RightLowerLeg, cache.RightFoot, color);
			VisualsHelpers::DrawBone(pDrawList, pPlayerController, entity.Mesh, cache.RightFoot, cache.RightToe, color);
		}

		if (drawHighlight)
			entity.Character->Multicast_SetMarked(true);
	}

	for (auto& item : m_vItemData)
	{
		if (!drawItems)
			continue;

		if (!pPlayerController->ProjectWorldLocationToScreen(item.WorldLocation, &item.ScreenLocation, false))
			continue;

		ImVec2 vecTextSize = ImGui::CalcTextSize(item.Name.c_str());

		ImU32 itemColor = Types::ItemType::Cash == item.Type ? itemCashColor :
			Types::ItemType::DepositBox == item.Type ? itemDepositBoxColor :
			Types::ItemType::Keycard == item.Type ? itemKeycardColor : IM_COL32(255, 255, 255, 255);

		pDrawList->AddText(
			ImVec2(item.ScreenLocation.X - vecTextSize.x / 2 - 1, item.ScreenLocation.Y - vecTextSize.y / 2 - 1),
			IM_COL32(0, 0, 0, 255), item.Name.c_str()
		);

		pDrawList->AddText(
			ImVec2(item.ScreenLocation.X - vecTextSize.x / 2, item.ScreenLocation.Y - vecTextSize.y / 2),
			itemColor, item.Name.c_str()
		);
	}
}

void Visuals::Run()
{
}
