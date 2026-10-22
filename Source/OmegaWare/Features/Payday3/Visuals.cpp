#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#pragma warning(push)
#pragma warning(disable : 4005 4369 4309 4267 4244)
#include "SDK/BP_QRPhone_classes.hpp"
#pragma warning(pop)

#include "Config/Config.hpp"
#include "Core/Log.hpp"
#include "Engines/Unreal/Field.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace OmegaWare
{
	namespace
	{
		const OmegaGUI::color kOutline(0, 0, 0, 255);

		enum EBone : std::size_t
		{
			Hips,
			Spine3,
			Neck,
			Head,
			LeftShoulder,
			LeftUpperArm,
			LeftForeArm,
			LeftHand,
			RightShoulder,
			RightUpperArm,
			RightForeArm,
			RightHand,
			LeftUpperLeg,
			LeftLowerLeg,
			LeftFoot,
			LeftToe,
			RightUpperLeg,
			RightLowerLeg,
			RightFoot,
			RightToe,
			BoneCount,
		};

		constexpr std::array<std::string_view, BoneCount> g_aBoneNames = {
			"Hips", "Spine3", "Neck", "Head",
			"LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
			"RightShoulder", "RightArm", "RightForeArm", "RightHand",
			"LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase",
			"RightUpLeg", "RightLeg", "RightFoot", "RightToeBase",
		};

		constexpr std::array<std::pair<EBone, EBone>, 19> g_aSkeleton = { {
			{ Head, Neck }, { Neck, Spine3 }, { Spine3, Hips },
			{ Spine3, LeftShoulder }, { LeftShoulder, LeftUpperArm }, { LeftUpperArm, LeftForeArm }, { LeftForeArm, LeftHand },
			{ Spine3, RightShoulder }, { RightShoulder, RightUpperArm }, { RightUpperArm, RightForeArm }, { RightForeArm, RightHand },
			{ Hips, LeftUpperLeg }, { LeftUpperLeg, LeftLowerLeg }, { LeftLowerLeg, LeftFoot }, { LeftFoot, LeftToe },
			{ Hips, RightUpperLeg }, { RightUpperLeg, RightLowerLeg }, { RightLowerLeg, RightFoot }, { RightFoot, RightToe },
		} };

		using BoneCache_t = std::array<int, BoneCount>;

		struct stBox
		{
			float flLeft = 0.f;
			float flTop = 0.f;
			float flRight = 0.f;
			float flBottom = 0.f;
		};

		struct stPawnStats
		{
			float flHealth = 0.f;
			float flHealthMax = 0.f;
			float flArmor = 0.f;
			float flArmorMax = 0.f;
		};

		struct stColorPair
		{
			OmegaGUI::color* pCop = nullptr;
			OmegaGUI::color* pCivilian = nullptr;

			[[nodiscard]] OmegaGUI::color Get(bool bCop) const { return bCop ? *pCop : *pCivilian; }
		};

		std::string FormatEntityName(std::string_view strName)
		{
			if (strName.empty() || strName.size() >= 256)
				return "Unknown";
			if (std::ranges::any_of(strName, [](char chLetter) { return chLetter < 32 || chLetter > 126; }))
				return "Invalid";
			return std::string(strName);
		}

		std::string FormatDistance(float flDistance)
		{
			return flDistance >= 0.f && flDistance < 10000.f ? std::format("[{:.0f}m]", flDistance) : "[???m]";
		}

		std::optional<OmegaGUI::vec2> Project(SDK::APlayerController* pController, const SDK::FVector& vecWorld)
		{
			SDK::FVector2D vecScreen;
			if (!pController->ProjectWorldLocationToScreen(vecWorld, &vecScreen, false))
				return std::nullopt;
			return OmegaGUI::vec2{ static_cast<float>(vecScreen.X), static_cast<float>(vecScreen.Y) };
		}

		std::optional<stPawnStats> ReadPawnStats(SDK::ASBZCharacter* pCharacter)
		{
			SDK::USBZAbilitySystemComponent* pAbilitySystem = pCharacter->AbilitySystem;
			if (!Memory::IsValidObjectPtr(pAbilitySystem))
				return std::nullopt;

			SDK::UAbilitySystemComponent* pBase = pAbilitySystem;
			SDK::TArray<SDK::UAttributeSet*>& aAttributes = OW_FIELD(pBase, SpawnedAttributes);
			for (int iAttribute = 0; iAttribute < aAttributes.Num(); ++iAttribute)
			{
				SDK::UAttributeSet* pSet = aAttributes[iAttribute];
				if (!Memory::IsValidObjectPtr(pSet) || !pSet->IsA(SDK::USBZPawnAttributeSet::StaticClass()))
					continue;
				auto* pPawnSet = static_cast<SDK::USBZPawnAttributeSet*>(pSet);
				return stPawnStats{ pPawnSet->Health.CurrentValue, pPawnSet->HealthMax.CurrentValue, pPawnSet->Armor.CurrentValue, pPawnSet->ArmorMax.CurrentValue };
			}
			return std::nullopt;
		}

		std::optional<stBox> CalculateBox(SDK::APlayerController* pController, SDK::AActor* pActor, SDK::USkeletalMeshComponent* pMesh, const SDK::FVector& vecCamera)
		{
			static const SDK::FName s_nameTop = Payday3::MakeName(L"HeadEnd");
			static const SDK::FName s_nameBottom = Payday3::MakeName(L"Reference");
			const std::optional<OmegaGUI::vec2> optTop = Project(pController, pMesh->GetSocketLocation(s_nameTop));
			const std::optional<OmegaGUI::vec2> optBottom = Project(pController, pMesh->GetSocketLocation(s_nameBottom));
			if (!optTop || !optBottom)
				return std::nullopt;

			const float flHeight = std::abs(optTop->y - optBottom->y);
			const double dYaw = pActor->K2_GetActorRotation().Yaw * (std::numbers::pi / 180.0);
			const SDK::FVector vecFacing = SDK::FVector(std::cos(dYaw), std::sin(dYaw), 0.0).GetNormalized();
			const SDK::FVector vecToActor = (pActor->K2_GetActorLocation() - vecCamera).GetNormalized();
			const float flWidth = flHeight * (.25f + static_cast<float>(std::abs(vecFacing.Dot(vecToActor))) * .15f);
			const float flCenter = (optTop->x + optBottom->x) * .5f;
			return stBox{ flCenter - flWidth * .5f, std::min(optTop->y, optBottom->y), flCenter + flWidth * .5f, std::max(optTop->y, optBottom->y) };
		}

		void DrawOutlinedText(OmegaGUI::clDrawList& rList, OmegaGUI::vec2 vecPosition, OmegaGUI::color col, std::string_view strText)
		{
			rList.AddText(vecPosition - OmegaGUI::vec2{ 1.f, 1.f }, kOutline, strText);
			rList.AddText(vecPosition, col, strText);
		}

		void DrawNameAndDistance(OmegaGUI::clDrawList& rList, float flCenterX, float flBottom, std::string_view strName, OmegaGUI::color colName, std::string_view strDistance, OmegaGUI::color colDistance)
		{
			const OmegaGUI::vec2 vecNameSize = OmegaGUI::CalcTextSize(strName);
			const OmegaGUI::vec2 vecDistanceSize = OmegaGUI::CalcTextSize(strDistance);
			const float flSpace = !strName.empty() && !strDistance.empty() ? OmegaGUI::CalcTextSize(" ").x : 0.f;
			const float flLineHeight = std::max(vecNameSize.y, vecDistanceSize.y);
			OmegaGUI::vec2 vecText{ flCenterX - (vecNameSize.x + flSpace + vecDistanceSize.x) * .5f, flBottom - flLineHeight };
			if (!strName.empty())
			{
				DrawOutlinedText(rList, vecText, colName, strName);
				vecText.x += vecNameSize.x + flSpace;
			}
			if (!strDistance.empty())
				DrawOutlinedText(rList, vecText, colDistance, strDistance);
		}

		enum class ECategory : std::uint8_t
		{
			Enemies,
			Civilians,
			Loot,
			KeyItems,
			Objectives,
		};

		struct stCategory
		{
			std::string_view strKey;
			std::string_view strTab;
			bool bPeople = false;
			bool* pBox = nullptr;
			bool* pName = nullptr;
			bool* pDistance = nullptr;
			bool* pHealthBar = nullptr;
			bool* pArmorBar = nullptr;
			bool* pSkeleton = nullptr;
			bool* pOutline = nullptr;

			[[nodiscard]] static bool On(const bool* pValue) { return pValue && *pValue; }
			[[nodiscard]] bool AnyDrawn() const { return On(pBox) || On(pName) || On(pDistance) || On(pHealthBar) || On(pArmorBar) || On(pSkeleton); }
		};

		struct stItemSetting
		{
			Payday3::EItemType eType;
			std::string_view strShow;
			std::string_view strColor;
			OmegaGUI::color colDefault;
			bool* pShow = nullptr;
			OmegaGUI::color* pColor = nullptr;
		};

		struct stOutlineSetting
		{
			const wchar_t* wszParameter;
			std::string_view strColor;
			OmegaGUI::color colDefault;
			OmegaGUI::color* pColor = nullptr;
		};

		class clVisualsFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "visuals"; }

			bool Setup() override
			{
				for (stCategory& rCategory : m_aCategories)
				{
					auto fnAdd = [&](std::string_view strOption) { return &Config::Add(std::format("visuals.{}.{}", rCategory.strKey, strOption), false); };
					rCategory.pName = fnAdd("name");
					rCategory.pDistance = fnAdd("distance");
					rCategory.pOutline = fnAdd("outline");
					if (!rCategory.bPeople)
						continue;
					rCategory.pBox = fnAdd("box");
					rCategory.pHealthBar = fnAdd("health_bar");
					rCategory.pSkeleton = fnAdd("skeleton");
				}
				Category(ECategory::Enemies).pArmorBar = &Config::Add("visuals.enemies.armor_bar", false);
				m_pCameras = &Config::Add("visuals.enemies.cameras", false);

				m_stBoxColors = { &Config::Add("visuals.box_cop_color", OmegaGUI::color(255, 0, 0)), &Config::Add("visuals.box_civilian_color", OmegaGUI::color(0, 255, 0)) };
				m_stNameColors = { &Config::Add("visuals.name_cop_color", OmegaGUI::color(255, 255, 255)), &Config::Add("visuals.name_civilian_color", OmegaGUI::color(0, 0, 255)) };
				m_stDistanceColors = { &Config::Add("visuals.distance_cop_color", OmegaGUI::color(255, 0, 0)), &Config::Add("visuals.distance_civilian_color", OmegaGUI::color(0, 255, 0)) };
				m_stHealthColors = { &Config::Add("visuals.health_bar_cop_color", OmegaGUI::color(0, 255, 0)), &Config::Add("visuals.health_bar_civilian_color", OmegaGUI::color(255, 0, 0)) };
				m_pArmorColor = &Config::Add("visuals.armor_bar_color", OmegaGUI::color(0, 128, 255));
				m_stSkeletonColors = { &Config::Add("visuals.skeleton_cop_color", OmegaGUI::color(255, 255, 0)), &Config::Add("visuals.skeleton_civilian_color", OmegaGUI::color(0, 255, 255)) };
				m_pShowDropOffs = &Config::Add("visuals.show_drop_offs", true);
				m_pDropOffColor = &Config::Add("visuals.drop_off_color", OmegaGUI::color(0, 255, 128));
				for (stItemSetting& rSetting : m_aItems)
				{
					rSetting.pShow = &Config::Add(rSetting.strShow, true);
					rSetting.pColor = &Config::Add(rSetting.strColor, rSetting.colDefault);
				}
				m_pOutlineColors = &Config::Add("visuals.outline_colors", false);
				for (stOutlineSetting& rSetting : m_aOutlines)
					rSetting.pColor = &Config::Add(rSetting.strColor, rSetting.colDefault);
				return true;
			}

			void Destroy() override
			{
				if (Payday3::IsReady() && (m_bOutlinesApplied || m_bOutlinesActive))
					Unreal::RunOnGameThreadAndWait([this] { RestoreAllOutlined(); RestoreOutlines(); }, std::chrono::seconds(1));
			}

			void Render() override
			{
				const stCategory& rEnemies = Category(ECategory::Enemies);
				const bool bPeople = rEnemies.AnyDrawn() || Category(ECategory::Civilians).AnyDrawn();
				const bool bCameras = *m_pCameras && (*rEnemies.pBox || *rEnemies.pName || *rEnemies.pDistance);
				const bool bThings = bCameras || Category(ECategory::Loot).AnyDrawn() || Category(ECategory::KeyItems).AnyDrawn() || Category(ECategory::Objectives).AnyDrawn();
				if ((!bPeople && !bThings) || !Payday3::IsReady())
					return;

				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				if (!Memory::IsValidObjectPtr(pWorld->PersistentLevel) || !pController || !Memory::IsValidObjectPtr(pController->PlayerCameraManager))
					return;

				if (pWorld != m_pLastWorld)
				{
					m_mapBones.clear();
					m_pLastWorld = pWorld;
				}

				OmegaGUI::clDrawList& rList = OmegaGUI::GetBackgroundDrawList();
				const SDK::FVector vecCamera = pController->PlayerCameraManager->GetCameraLocation();
				if (bPeople)
					DrawCharacters(rList, pController, vecCamera);
				if (bThings)
					DrawThings(rList, pController, pWorld, vecCamera, bCameras);
			}

			void Run() override
			{
				QueueOutlineColors();
				using Clock_t = std::chrono::steady_clock;
				if ((!AnyOutline() && !m_bOutlinesActive) || !Payday3::IsReady() || Clock_t::now() < m_tpNextOutlineUpdate || m_bOutlineUpdateQueued.exchange(true))
					return;
				m_tpNextOutlineUpdate = Clock_t::now() + std::chrono::milliseconds(100);
				if (!Unreal::RunOnGameThread([this] {
						m_bOutlineUpdateQueued = false;
						if (IsActive() && AnyOutline())
							UpdateOutlined();
						else
							RestoreAllOutlined();
					}))
					m_bOutlineUpdateQueued = false;
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				if (!OmegaGUI::BeginTabBar("visuals.tabs"))
					return;
				for (std::size_t uCategory = 0; uCategory < m_aCategories.size(); ++uCategory)
				{
					const stCategory& rCategory = m_aCategories[uCategory];
					if (!OmegaGUI::BeginTabItem(std::format("{}###visuals.tab_{}", OmegaLocalization::Get(rCategory.strTab), rCategory.strKey)))
						continue;
					OmegaGUI::BeginColumns(2);
					if (OmegaGUI::BeginSection(std::format("{}###visuals.{}.section_options", "visuals.section_options"_t, rCategory.strKey)))
					{
						DrawCategoryOptions(static_cast<ECategory>(uCategory));
						OmegaGUI::EndSection();
					}
					OmegaGUI::NextColumn();
					if (OmegaGUI::BeginSection(std::format("{}###visuals.{}.section_colors", "visuals.section_colors"_t, rCategory.strKey)))
					{
						DrawCategoryColors(static_cast<ECategory>(uCategory));
						OmegaGUI::EndSection();
					}
					OmegaGUI::EndColumns();
					OmegaGUI::EndTabItem();
				}
				if (OmegaGUI::BeginTabItem("visuals.group_outlines"_l))
				{
					DrawOutlinesTab();
					OmegaGUI::EndTabItem();
				}
				OmegaGUI::EndTabBar();
			}

		private:
			static constexpr int kPeopleOutlineIndex = 3;
			static constexpr int kItemOutlineIndex = 9;

			struct stOutlineSwap
			{
				SDK::UPostProcessComponent* pComponent;
				SDK::UMaterialInterface* pOriginal;
				SDK::UMaterialInstanceDynamic* pDynamic;
			};

			struct stOutlined
			{
				std::vector<SDK::UMeshComponent*> aMeshes;
				SDK::USBZOutlineComponent* pOutline = nullptr;
			};

			[[nodiscard]] stCategory& Category(ECategory eCategory) { return m_aCategories[static_cast<std::size_t>(eCategory)]; }
			[[nodiscard]] const stCategory& Category(ECategory eCategory) const { return m_aCategories[static_cast<std::size_t>(eCategory)]; }

			[[nodiscard]] bool AnyOutline() const
			{
				return std::ranges::any_of(m_aCategories, [](const stCategory& rCategory) { return *rCategory.pOutline; });
			}

			[[nodiscard]] static ECategory CategoryOf(Payday3::EItemType eType)
			{
				switch (eType)
				{
				case Payday3::EItemType::Cash:
				case Payday3::EItemType::MoneyMedium:
				case Payday3::EItemType::MoneyLarge:
				case Payday3::EItemType::Jewelry:
					return ECategory::Loot;
				case Payday3::EItemType::KeycardReader:
					return ECategory::Objectives;
				default:
					return ECategory::KeyItems;
				}
			}

			[[nodiscard]] const stItemSetting* Setting(Payday3::EItemType eType) const
			{
				const auto itSetting = std::ranges::find(m_aItems, eType, &stItemSetting::eType);
				return itSetting != m_aItems.end() ? &*itSetting : nullptr;
			}

			void DrawCategoryOptions(ECategory eCategory)
			{
				using namespace OmegaLocalization::Literals;
				stCategory& rCategory = Category(eCategory);
				auto fnOption = [&](std::string_view strText, std::string_view strOption, bool* pValue) {
					if (pValue)
						OmegaGUI::Checkbox(std::format("{}###visuals.{}.{}", OmegaLocalization::Get(strText), rCategory.strKey, strOption), pValue);
				};
				fnOption("visuals.box", "box", rCategory.pBox);
				fnOption("visuals.name", "name", rCategory.pName);
				fnOption("visuals.distance", "distance", rCategory.pDistance);
				fnOption("visuals.health_bar", "health_bar", rCategory.pHealthBar);
				fnOption("visuals.armor_bar", "armor_bar", rCategory.pArmorBar);
				fnOption("visuals.skeleton", "skeleton", rCategory.pSkeleton);
				fnOption("visuals.outline", "outline", rCategory.pOutline);
				OmegaGUI::ItemTooltip(rCategory.bPeople ? "visuals.outline_people_hint"_t : "visuals.outline_items_hint"_t);
				if (eCategory == ECategory::Enemies)
				{
					OmegaGUI::Separator();
					OmegaGUI::Checkbox("visuals.cameras"_l, m_pCameras);
					OmegaGUI::ItemTooltip("visuals.cameras_hint"_t);
				}
				if (rCategory.bPeople)
					return;
				OmegaGUI::Separator();
				for (const stItemSetting& rSetting : m_aItems)
					if (CategoryOf(rSetting.eType) == eCategory)
						OmegaGUI::Checkbox(std::format("{}###{}", OmegaLocalization::Get(rSetting.strShow), rSetting.strShow), rSetting.pShow);
				if (eCategory == ECategory::Objectives)
					OmegaGUI::Checkbox("visuals.show_drop_offs"_l, m_pShowDropOffs);
			}

			void DrawCategoryColors(ECategory eCategory)
			{
				using namespace OmegaLocalization::Literals;
				if (!OmegaGUI::BeginTable(std::format("##visuals.{}.colors", Category(eCategory).strKey), 2, OmegaGUI::ETableFlags::RowBackground))
					return;
				OmegaGUI::TableSetupColumn({}, OmegaGUI::ETableColumnFlags::None, 1.3f);
				OmegaGUI::TableSetupColumn({}, OmegaGUI::ETableColumnFlags::None, 1.f);
				auto fnRow = [](std::string_view strText, std::string_view strID, OmegaGUI::color* pColor) {
					OmegaGUI::TableNextRow();
					OmegaGUI::TableNextColumn();
					OmegaGUI::TextUnformatted(OmegaLocalization::Get(strText));
					OmegaGUI::TableNextColumn();
					OmegaGUI::ColorEdit(std::format("###{}", strID), pColor);
				};
				if (eCategory == ECategory::Enemies || eCategory == ECategory::Civilians)
				{
					const bool bCop = eCategory == ECategory::Enemies;
					auto fnPair = [&](std::string_view strText, std::string_view strCop, std::string_view strCivilian, const stColorPair& stColors) {
						fnRow(strText, bCop ? strCop : strCivilian, bCop ? stColors.pCop : stColors.pCivilian);
					};
					fnPair("visuals.box", "visuals.box_cop_color", "visuals.box_civilian_color", m_stBoxColors);
					fnPair("visuals.name", "visuals.name_cop_color", "visuals.name_civilian_color", m_stNameColors);
					fnPair("visuals.distance", "visuals.distance_cop_color", "visuals.distance_civilian_color", m_stDistanceColors);
					fnPair("visuals.health_bar", "visuals.health_bar_cop_color", "visuals.health_bar_civilian_color", m_stHealthColors);
					if (bCop)
						fnRow("visuals.armor_bar", "visuals.armor_bar_color", m_pArmorColor);
					fnPair("visuals.skeleton", "visuals.skeleton_cop_color", "visuals.skeleton_civilian_color", m_stSkeletonColors);
				}
				else
				{
					for (const stItemSetting& rSetting : m_aItems)
						if (CategoryOf(rSetting.eType) == eCategory)
							fnRow(rSetting.strShow, rSetting.strColor, rSetting.pColor);
					if (eCategory == ECategory::Objectives)
						fnRow("visuals.show_drop_offs", "visuals.drop_off_color", m_pDropOffColor);
				}
				OmegaGUI::EndTable();
			}

			void DrawOutlinesTab()
			{
				using namespace OmegaLocalization::Literals;
				if (OmegaGUI::BeginSection(std::format("{}###visuals.outlines.section_colors", "visuals.section_colors"_t)))
				{
					OmegaGUI::TextMuted("visuals.outline_slots_hint"_t);
					OmegaGUI::Checkbox("visuals.outline_colors"_l, m_pOutlineColors);
					OmegaGUI::ItemTooltip("visuals.outline_colors_hint"_t);
					if (OmegaGUI::BeginTable("##visuals.outline_colors", 2, OmegaGUI::ETableFlags::RowBackground))
					{
						OmegaGUI::TableSetupColumn({}, OmegaGUI::ETableColumnFlags::None, 1.3f);
						OmegaGUI::TableSetupColumn({}, OmegaGUI::ETableColumnFlags::None, 1.f);
						for (const stOutlineSetting& rSetting : m_aOutlines)
						{
							OmegaGUI::TableNextRow();
							OmegaGUI::TableNextColumn();
							OmegaGUI::TextUnformatted(OmegaLocalization::Get(rSetting.strColor));
							OmegaGUI::TableNextColumn();
							OmegaGUI::ColorEdit(std::format("###{}", rSetting.strColor), rSetting.pColor);
						}
						OmegaGUI::EndTable();
					}
					if (OmegaGUI::SmallButton("visuals.outline_reset"_l))
						for (stOutlineSetting& rSetting : m_aOutlines)
							*rSetting.pColor = rSetting.colDefault;
					OmegaGUI::EndSection();
				}
			}

			void QueueOutlineColors()
			{
				using Clock_t = std::chrono::steady_clock;
				if ((!*m_pOutlineColors && !m_bOutlinesApplied) || !Payday3::IsReady() || Clock_t::now() < m_tpNextOutline || m_bOutlineQueued.exchange(true))
					return;
				m_tpNextOutline = Clock_t::now() + std::chrono::milliseconds(250);
				if (!Unreal::RunOnGameThread([this] {
						m_bOutlineQueued = false;
						if (IsActive() && *m_pOutlineColors)
							ApplyOutlines();
						else
							RestoreOutlines();
					}))
					m_bOutlineQueued = false;
			}

			[[nodiscard]] static bool IsOutlineMaterial(SDK::UObject* pMaterial)
			{
				for (int iDepth = 0; iDepth < 16 && Payday3::IsAlive(pMaterial) && pMaterial->IsA(SDK::UMaterialInterface::StaticClass()); ++iDepth)
				{
					const std::string strName = pMaterial->GetName();
					if (strName == "PP_Outline_V4_Inst" || strName == "PP_Outline_V4")
						return true;
					if (!pMaterial->IsA(SDK::UMaterialInstance::StaticClass()))
						return false;
					pMaterial = static_cast<SDK::UMaterialInstance*>(pMaterial)->Parent;
				}
				return false;
			}

			[[nodiscard]] SDK::UPostProcessComponent* FindOutlineComponent(SDK::UWorld* pWorld)
			{
				if (pWorld == m_pOutlineWorld && Payday3::IsAlive(m_pOutlineComponent))
					return m_pOutlineComponent;
				m_pOutlineWorld = pWorld;
				m_pOutlineComponent = nullptr;
				if (!Payday3::IsAliveActor(pWorld->GameState))
					return nullptr;
				const SDK::TArray<SDK::UActorComponent*> aComponents = pWorld->GameState->K2_GetComponentsByClass(SDK::UPostProcessComponent::StaticClass());
				for (SDK::UActorComponent* pComponent : aComponents)
				{
					if (Payday3::IsAlive(pComponent) && pComponent->GetName().find("OutlinePostProcess") != std::string::npos)
					{
						m_pOutlineComponent = static_cast<SDK::UPostProcessComponent*>(pComponent);
						break;
					}
				}
				return m_pOutlineComponent;
			}

			[[nodiscard]] std::vector<SDK::UMaterialInstanceDynamic*> OutlineMaterials()
			{
				std::vector<SDK::UMaterialInstanceDynamic*> aMaterials;
				std::erase_if(m_aOutlineSwaps, [](const stOutlineSwap& rSwap) { return !Payday3::IsAlive(rSwap.pComponent); });
				SDK::UWorld* pWorld = Unreal::GetWorld();
				if (!Payday3::IsAlive(pWorld))
					return aMaterials;
				SDK::UPostProcessComponent* pComponent = FindOutlineComponent(pWorld);
				if (!pComponent)
					return aMaterials;
				for (SDK::FWeightedBlendable& rBlendable : pComponent->Settings.WeightedBlendables.Array)
				{
					if (!IsOutlineMaterial(rBlendable.Object))
						continue;
					if (rBlendable.Object->IsA(SDK::UMaterialInstanceDynamic::StaticClass()))
					{
						aMaterials.push_back(static_cast<SDK::UMaterialInstanceDynamic*>(rBlendable.Object));
						continue;
					}
					auto* pOriginal = static_cast<SDK::UMaterialInterface*>(rBlendable.Object);
					SDK::UMaterialInstanceDynamic* pDynamic = SDK::UKismetMaterialLibrary::CreateDynamicMaterialInstance(pWorld, pOriginal, SDK::FName{}, SDK::EMIDCreationFlags::None);
					if (!Payday3::IsAlive(pDynamic))
						continue;
					LogInfo("outline colors: replaced {} on {} with a runtime copy", pOriginal->GetName(), pComponent->GetName());
					m_aOutlineSwaps.push_back({ pComponent, pOriginal, pDynamic });
					rBlendable.Object = pDynamic;
					aMaterials.push_back(pDynamic);
				}
				return aMaterials;
			}

			[[nodiscard]] static SDK::FLinearColor ToLinear(OmegaGUI::color col)
			{
				auto fnChannel = [](std::uint8_t byValue) {
					const float flValue = byValue / 255.f;
					return flValue <= .04045f ? flValue / 12.92f : std::pow((flValue + .055f) / 1.055f, 2.4f);
				};
				return { fnChannel(col.R()), fnChannel(col.G()), fnChannel(col.B()), col.A() / 255.f };
			}

			[[nodiscard]] SDK::FName OutlineName(const wchar_t* wszParameter)
			{
				auto itName = m_mapOutlineNames.find(wszParameter);
				if (itName == m_mapOutlineNames.end())
					itName = m_mapOutlineNames.emplace(wszParameter, Payday3::MakeName(wszParameter)).first;
				return itName->second;
			}

			void ApplyOutlines()
			{
				for (SDK::UMaterialInstanceDynamic* pMaterial : OutlineMaterials())
				{
					const bool bOwnCopy = std::ranges::any_of(m_aOutlineSwaps, [&](const stOutlineSwap& rSwap) { return rSwap.pDynamic == pMaterial; });
					for (const stOutlineSetting& rSetting : m_aOutlines)
					{
						const SDK::FName nameParameter = OutlineName(rSetting.wszParameter);
						const auto prKey = std::make_pair(pMaterial, std::wstring_view(rSetting.wszParameter));
						if (!bOwnCopy && !m_mapOutlineOriginals.contains(prKey))
							m_mapOutlineOriginals.emplace(prKey, pMaterial->K2_GetVectorParameterValue(nameParameter));
						pMaterial->SetVectorParameterValue(nameParameter, ToLinear(*rSetting.pColor));
					}
				}
				m_bOutlinesApplied = !m_aOutlineSwaps.empty() || !m_mapOutlineOriginals.empty();
			}

			void RestoreOutlines()
			{
				for (const stOutlineSwap& rSwap : m_aOutlineSwaps)
				{
					if (!Payday3::IsAlive(rSwap.pComponent) || !Payday3::IsAlive(rSwap.pOriginal))
						continue;
					for (SDK::FWeightedBlendable& rBlendable : rSwap.pComponent->Settings.WeightedBlendables.Array)
						if (rBlendable.Object == rSwap.pDynamic)
							rBlendable.Object = rSwap.pOriginal;
					LogInfo("outline colors: put {} back on {}", rSwap.pOriginal->GetName(), rSwap.pComponent->GetName());
				}
				m_aOutlineSwaps.clear();
				for (const auto& [prKey, stOriginal] : m_mapOutlineOriginals)
					if (Payday3::IsAlive(prKey.first))
						prKey.first->SetVectorParameterValue(OutlineName(prKey.second.data()), stOriginal);
				m_mapOutlineOriginals.clear();
				m_bOutlinesApplied = false;
			}

			static void SetMeshStencil(const std::vector<SDK::UMeshComponent*>& aMeshes, bool bEnabled, int iColorIndex)
			{
				for (SDK::UMeshComponent* pMesh : aMeshes)
				{
					if (!Payday3::IsAlive(pMesh))
						continue;
					const int iStencil = bEnabled ? iColorIndex : 0;
					if (pMesh->CustomDepthStencilValue != iStencil)
						pMesh->SetCustomDepthStencilValue(iStencil);
					if (static_cast<bool>(pMesh->bRenderCustomDepth) != bEnabled)
						pMesh->SetRenderCustomDepth(bEnabled);
				}
			}

			stOutlined& Outlined(SDK::AActor* pActor, SDK::USBZOutlineComponent* pOutline)
			{
				if (const auto itOutlined = m_mapOutlined.find(pActor); itOutlined != m_mapOutlined.end())
					return itOutlined->second;
				stOutlined stEntry;
				stEntry.pOutline = Payday3::IsAlive(pOutline) ? pOutline : static_cast<SDK::USBZOutlineComponent*>(pActor->GetComponentByClass(SDK::USBZOutlineComponent::StaticClass()));
				if (Payday3::IsAlive(stEntry.pOutline))
					for (SDK::UMeshComponent* pMesh : stEntry.pOutline->Meshes)
						if (Payday3::IsAlive(pMesh))
							stEntry.aMeshes.push_back(pMesh);
				if (stEntry.aMeshes.empty())
				{
					const SDK::TArray<SDK::UActorComponent*> aComponents = pActor->K2_GetComponentsByClass(SDK::UMeshComponent::StaticClass());
					for (SDK::UActorComponent* pComponent : aComponents)
						if (Payday3::IsAlive(pComponent))
							stEntry.aMeshes.push_back(static_cast<SDK::UMeshComponent*>(pComponent));
				}
				return m_mapOutlined.emplace(pActor, std::move(stEntry)).first->second;
			}

			static void RestoreOutlined(const stOutlined& rEntry)
			{
				SDK::USBZOutlineComponent* pOutline = rEntry.pOutline;
				SDK::USBZOutlineAsset* pAsset = Payday3::IsAlive(pOutline) ? pOutline->ActiveAsset : nullptr;
				const bool bGameOutline = Payday3::IsAlive(pAsset) && !pOutline->bIsReplicatedHidden;
				SetMeshStencil(rEntry.aMeshes, bGameOutline, bGameOutline ? pAsset->ColorIndex : 0);
			}

			void RestoreAllOutlined()
			{
				for (const auto& [pActor, rEntry] : m_mapOutlined)
					if (Payday3::IsAliveActor(pActor))
						RestoreOutlined(rEntry);
				m_mapOutlined.clear();
				m_bOutlinesActive = false;
			}

			void UpdateOutlined()
			{
				std::erase_if(m_mapOutlined, [](const auto& rEntry) { return !Payday3::IsAliveActor(rEntry.first); });

				std::unordered_map<SDK::AActor*, std::pair<int, SDK::USBZOutlineComponent*>> mapWanted;
				SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns();
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				if (pPawns && pController)
				{
					SDK::APawn* pLocalPawn = pController->AcknowledgedPawn;
					SDK::TArray<SDK::UObject*>& aActors = pPawns->Objects;
					for (int iActor = 0; iActor < aActors.Num(); ++iActor)
					{
						auto* pActor = static_cast<SDK::AActor*>(aActors[iActor]);
						if (!Payday3::IsAliveActor(pActor) || pActor == pLocalPawn)
							continue;
						auto* pCharacter = static_cast<SDK::ASBZCharacter*>(pActor);
						const Payday3::EEnemyCategory eKind = Payday3::GetEnemyInfo(Payday3::ResolveEnemyType(pActor)).eCategory;
						if (eKind != Payday3::EEnemyCategory::Cop && eKind != Payday3::EEnemyCategory::Civilian)
							continue;
						const ECategory eCategory = eKind == Payday3::EEnemyCategory::Cop ? ECategory::Enemies : ECategory::Civilians;
						if (pCharacter->bIsAlive && *Category(eCategory).pOutline)
							mapWanted[pActor] = { kPeopleOutlineIndex, pCharacter->OutlineComponent };
					}
				}

				const bool bCameras = *m_pCameras && *Category(ECategory::Enemies).pOutline;
				const bool bItems = *Category(ECategory::Loot).pOutline || *Category(ECategory::KeyItems).pOutline || *Category(ECategory::Objectives).pOutline;
				if (bCameras || bItems)
				{
					Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
						if (!Payday3::IsAliveActor(pActor))
							return;
						if (pActor->IsA(SDK::ASBZSecurityCamera::StaticClass()))
						{
							auto* pCamera = static_cast<SDK::ASBZSecurityCamera*>(pActor);
							if (bCameras && pCamera->CameraState != SDK::ESBZCameraState::Destroyed)
								mapWanted[pActor] = { kPeopleOutlineIndex, pCamera->OutlineComponent };
							return;
						}
						if (!bItems)
							return;
						const Payday3::stItemInfo& rItem = Payday3::ResolveItem(pActor);
						const stItemSetting* pSetting = Setting(rItem.eType);
						if (!pSetting || !*pSetting->pShow || !*Category(CategoryOf(rItem.eType)).pOutline)
							return;
						if (rItem.eType == Payday3::EItemType::QRPhone && IsCollectedPhone(pActor))
							return;
						mapWanted[pActor] = { kItemOutlineIndex, nullptr };
					});
				}

				for (auto itOutlined = m_mapOutlined.begin(); itOutlined != m_mapOutlined.end();)
				{
					if (mapWanted.contains(itOutlined->first))
					{
						++itOutlined;
						continue;
					}
					RestoreOutlined(itOutlined->second);
					itOutlined = m_mapOutlined.erase(itOutlined);
				}
				for (const auto& [pActor, prWanted] : mapWanted)
					SetMeshStencil(Outlined(pActor, prWanted.second).aMeshes, true, prWanted.first);
				m_bOutlinesActive = !m_mapOutlined.empty();
			}

			void DrawCharacters(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, const SDK::FVector& vecCamera)
			{
				SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns();
				if (!pPawns)
					return;

				SDK::APawn* pLocalPawn = pController->AcknowledgedPawn;
				SDK::TArray<SDK::UObject*>& aActors = pPawns->Objects;
				for (int iActor = 0; iActor < aActors.Num(); ++iActor)
				{
					auto* pActor = static_cast<SDK::AActor*>(aActors[iActor]);
					if (!Memory::IsValidObjectPtr(pActor) || pActor == pLocalPawn)
						continue;
					auto* pCharacter = static_cast<SDK::ASBZCharacter*>(pActor);
					if (!pCharacter->bIsAlive || !Memory::IsValidObjectPtr(pCharacter->Mesh))
						continue;

					const Payday3::stEnemyInfo& stInfo = Payday3::GetEnemyInfo(Payday3::ResolveEnemyType(pActor));
					if (stInfo.eCategory != Payday3::EEnemyCategory::Cop && stInfo.eCategory != Payday3::EEnemyCategory::Civilian)
						continue;
					const bool bCop = stInfo.eCategory == Payday3::EEnemyCategory::Cop;
					const stCategory& rCategory = Category(bCop ? ECategory::Enemies : ECategory::Civilians);
					if (!rCategory.AnyDrawn())
						continue;

					if (stCategory::On(rCategory.pBox) || *rCategory.pName || *rCategory.pDistance || stCategory::On(rCategory.pHealthBar) || stCategory::On(rCategory.pArmorBar))
					{
						const std::optional<stBox> optBox = CalculateBox(pController, pActor, pCharacter->Mesh, vecCamera);
						if (!optBox)
							continue;
						const bool bStats = stCategory::On(rCategory.pHealthBar) || stCategory::On(rCategory.pArmorBar);
						const stPawnStats stStats = bStats ? ReadPawnStats(pCharacter).value_or(stPawnStats{}) : stPawnStats{};
						const float flDistance = static_cast<float>((pActor->K2_GetActorLocation() - vecCamera).Magnitude() / 100.0);
						DrawBox(rList, *optBox, stInfo.strName, flDistance, stStats, rCategory, bCop);
					}

					if (stCategory::On(rCategory.pSkeleton))
						DrawSkeleton(rList, pController, pCharacter->Mesh, m_stSkeletonColors.Get(bCop));
				}
			}

			void DrawBox(OmegaGUI::clDrawList& rList, const stBox& stRect, std::string_view strName, float flDistance, const stPawnStats& stStats, const stCategory& rCategory, bool bCop) const
			{
				const float flBoxWidth = stRect.flRight - stRect.flLeft;
				const float flBoxHeight = stRect.flBottom - stRect.flTop;
				if (!std::isfinite(flBoxWidth) || !std::isfinite(flBoxHeight) || flBoxWidth <= 0.f || flBoxHeight <= 0.f)
					return;

				if (stCategory::On(rCategory.pBox))
				{
					rList.AddRect({ stRect.flLeft - 1.f, stRect.flTop - 1.f }, { stRect.flRight + 1.f, stRect.flBottom + 1.f }, kOutline, 0.f, 1.6f);
					rList.AddRect({ stRect.flLeft, stRect.flTop }, { stRect.flRight, stRect.flBottom }, m_stBoxColors.Get(bCop), 0.f, 1.5f);
				}

				if (*rCategory.pName || *rCategory.pDistance)
					DrawNameAndDistance(rList, stRect.flLeft + flBoxWidth * .5f, stRect.flTop - 4.f, *rCategory.pName ? FormatEntityName(strName) : std::string(), m_stNameColors.Get(bCop), *rCategory.pDistance ? FormatDistance(flDistance) : std::string(), m_stDistanceColors.Get(bCop));

				constexpr float flBarWidth = 4.f;
				if (stCategory::On(rCategory.pHealthBar))
				{
					const float flFraction = stStats.flHealthMax > 0.f ? std::clamp(stStats.flHealth / stStats.flHealthMax, 0.f, 1.f) : 0.f;
					rList.AddRectFilled({ stRect.flLeft - flBarWidth - 3.f, stRect.flTop }, { stRect.flLeft - 1.f, stRect.flBottom }, kOutline);
					rList.AddRectFilled({ stRect.flLeft - flBarWidth - 2.f, stRect.flBottom - flBoxHeight * flFraction }, { stRect.flLeft - 2.f, stRect.flBottom }, m_stHealthColors.Get(bCop));
				}

				if (stCategory::On(rCategory.pArmorBar))
				{
					const float flFraction = stStats.flArmorMax > 0.f ? std::clamp(stStats.flArmor / stStats.flArmorMax, 0.f, 1.f) : 0.f;
					const float flArmorHeight = flBoxHeight * flFraction;
					rList.AddRectFilled({ stRect.flLeft - flBarWidth * 2.f - 5.f, stRect.flBottom - flArmorHeight }, { stRect.flLeft - flBarWidth - 3.f, stRect.flBottom }, kOutline);
					rList.AddRectFilled({ stRect.flLeft - flBarWidth * 2.f - 4.f, stRect.flBottom - flArmorHeight }, { stRect.flLeft - flBarWidth - 4.f, stRect.flBottom }, *m_pArmorColor);
				}
			}

			const BoneCache_t& Bones(SDK::USkeletalMeshComponent* pMesh)
			{
				auto [itCache, bInserted] = m_mapBones.try_emplace(pMesh);
				if (!bInserted)
					return itCache->second;

				itCache->second.fill(-1);
				const int iBones = pMesh->GetNumBones();
				for (int iBone = 0; iBone < iBones; ++iBone)
				{
					const std::string strBone = pMesh->GetBoneName(iBone).ToString();
					if (auto itName = std::ranges::find(g_aBoneNames, strBone); itName != g_aBoneNames.end())
						itCache->second[static_cast<std::size_t>(itName - g_aBoneNames.begin())] = iBone;
				}
				return itCache->second;
			}

			void DrawSkeleton(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::USkeletalMeshComponent* pMesh, OmegaGUI::color col)
			{
				const BoneCache_t& aBones = Bones(pMesh);
				for (const auto& [eParent, eChild] : g_aSkeleton)
				{
					const int iParent = aBones[eParent];
					const int iChild = aBones[eChild];
					if (iParent < 0 || iChild < 0)
						continue;
					const std::optional<OmegaGUI::vec2> optParent = Project(pController, pMesh->GetSocketLocation(pMesh->GetBoneName(iParent)));
					const std::optional<OmegaGUI::vec2> optChild = Project(pController, pMesh->GetSocketLocation(pMesh->GetBoneName(iChild)));
					if (!optParent || !optChild)
						continue;
					rList.AddLine(*optParent - OmegaGUI::vec2{ 1.f, 1.f }, *optChild - OmegaGUI::vec2{ 1.f, 1.f }, kOutline, 1.1f);
					rList.AddLine(*optParent, *optChild, col, 1.f);
				}
			}

			[[nodiscard]] static bool IsCollectedPhone(SDK::AActor* pActor)
			{
				if (!pActor->IsA(SDK::ABP_QRPhone_C::StaticClass()))
					return false;
				auto* pPhone = static_cast<SDK::ABP_QRPhone_C*>(pActor);
				if (pActor->bHidden || pPhone->bState)
					return true;
				return Memory::IsValidObjectPtr(pPhone->MobilePhone) && !pPhone->MobilePhone->bVisible;
			}

			[[nodiscard]] static float DistanceMeters(SDK::AActor* pActor, const SDK::FVector& vecCamera)
			{
				return static_cast<float>((pActor->K2_GetActorLocation() - vecCamera).Magnitude() / 100.0);
			}

			void DrawCamera(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::ASBZSecurityCamera* pCamera, const SDK::FVector& vecCamera) const
			{
				if (pCamera->CameraState == SDK::ESBZCameraState::Destroyed || !Payday3::IsAliveActor(pCamera))
					return;
				const std::optional<OmegaGUI::vec2> optScreen = Project(pController, pCamera->K2_GetActorLocation());
				if (!optScreen)
					return;
				const stCategory& rEnemies = Category(ECategory::Enemies);
				const std::uint8_t byAlpha = pCamera->CameraState == SDK::ESBZCameraState::Enabled ? 255 : 110;
				if (*rEnemies.pBox)
				{
					rList.AddRect(*optScreen - OmegaGUI::vec2{ 5.f, 5.f }, *optScreen + OmegaGUI::vec2{ 5.f, 5.f }, kOutline, 0.f, 3.f);
					rList.AddRect(*optScreen - OmegaGUI::vec2{ 5.f, 5.f }, *optScreen + OmegaGUI::vec2{ 5.f, 5.f }, m_stBoxColors.pCop->WithAlpha(byAlpha), 0.f, 1.5f);
				}
				if (*rEnemies.pName || *rEnemies.pDistance)
				{
					const std::string_view strLabel = byAlpha == 255 ? "Camera" : "Camera (off)";
					DrawNameAndDistance(rList, optScreen->x, optScreen->y - 8.f, *rEnemies.pName ? strLabel : std::string_view(), m_stNameColors.pCop->WithAlpha(byAlpha), *rEnemies.pDistance ? FormatDistance(DistanceMeters(pCamera, vecCamera)) : std::string(), m_stDistanceColors.pCop->WithAlpha(byAlpha));
				}
			}

			void DrawDropOff(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::ASBZBagTriggerVolume* pVolume, const SDK::FVector& vecCamera) const
			{
				const std::optional<OmegaGUI::vec2> optScreen = Project(pController, pVolume->K2_GetActorLocation());
				if (!optScreen)
					return;
				const stCategory& rObjectives = Category(ECategory::Objectives);
				const bool bActive = pVolume->GetActorEnableCollision();
				const OmegaGUI::color col = bActive ? *m_pDropOffColor : m_pDropOffColor->WithAlpha(110);
				rList.AddCircle(*optScreen, 6.f, kOutline, 16, 3.f);
				rList.AddCircle(*optScreen, 6.f, col, 16, 1.5f);
				const std::string_view strLabel = bActive ? "Drop-off" : "Drop-off (inactive)";
				DrawNameAndDistance(rList, optScreen->x, optScreen->y - 10.f, *rObjectives.pName ? strLabel : std::string_view(), col, *rObjectives.pDistance ? FormatDistance(DistanceMeters(pVolume, vecCamera)) : std::string(), col);
			}

			void DrawThings(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::UWorld* pWorld, const SDK::FVector& vecCamera, bool bCameras)
			{
				const stCategory& rObjectives = Category(ECategory::Objectives);
				const bool bDropOffs = *m_pShowDropOffs && (*rObjectives.pName || *rObjectives.pDistance);
				Payday3::ForEachLevelActor(pWorld, [&](SDK::AActor* pActor) {
					if (pActor->IsA(SDK::ASBZSecurityCamera::StaticClass()))
					{
						if (bCameras)
							DrawCamera(rList, pController, static_cast<SDK::ASBZSecurityCamera*>(pActor), vecCamera);
						return;
					}

					if (bDropOffs)
					{
						if (SDK::ASBZBagTriggerVolume* pVolume = Payday3::AsDropOff(pActor))
						{
							DrawDropOff(rList, pController, pVolume, vecCamera);
							return;
						}
					}

					const Payday3::stItemInfo& rItem = Payday3::ResolveItem(pActor);
					const stItemSetting* pSetting = Setting(rItem.eType);
					if (!pSetting || !*pSetting->pShow)
						return;
					const stCategory& rCategory = Category(CategoryOf(rItem.eType));
					if (!*rCategory.pName && !*rCategory.pDistance)
						return;
					if (rItem.eType == Payday3::EItemType::QRPhone && IsCollectedPhone(pActor))
						return;

					const std::optional<OmegaGUI::vec2> optScreen = Project(pController, pActor->K2_GetActorLocation());
					if (!optScreen)
						return;
					const float flLineHeight = OmegaGUI::CalcTextSize(rItem.strLabel).y;
					DrawNameAndDistance(rList, optScreen->x, optScreen->y + flLineHeight * .5f, *rCategory.pName ? rItem.strLabel : std::string_view(), *pSetting->pColor, *rCategory.pDistance ? FormatDistance(DistanceMeters(pActor, vecCamera)) : std::string(), *pSetting->pColor);
				});
			}

			std::array<stCategory, 5> m_aCategories = { {
				{ "enemies", "visuals.tab_enemies", true },
				{ "civilians", "visuals.tab_civilians", true },
				{ "loot", "visuals.loot", false },
				{ "key_items", "visuals.items", false },
				{ "objectives", "visuals.tab_objectives", false },
			} };
			bool* m_pCameras = nullptr;
			stColorPair m_stBoxColors;
			stColorPair m_stNameColors;
			stColorPair m_stDistanceColors;
			stColorPair m_stHealthColors;
			OmegaGUI::color* m_pArmorColor = nullptr;
			stColorPair m_stSkeletonColors;
			bool* m_pShowDropOffs = nullptr;
			OmegaGUI::color* m_pDropOffColor = nullptr;
			std::array<stItemSetting, 9> m_aItems = { {
				{ Payday3::EItemType::Cash, "visuals.show_cash", "visuals.item_cash_color", OmegaGUI::color(0, 255, 255) },
				{ Payday3::EItemType::MoneyMedium, "visuals.show_money_medium", "visuals.item_money_medium_color", OmegaGUI::color(120, 220, 120) },
				{ Payday3::EItemType::MoneyLarge, "visuals.show_money_large", "visuals.item_money_large_color", OmegaGUI::color(0, 255, 0) },
				{ Payday3::EItemType::Jewelry, "visuals.show_jewelry", "visuals.item_jewelry_color", OmegaGUI::color(255, 0, 255) },
				{ Payday3::EItemType::DepositBox, "visuals.show_deposit_boxes", "visuals.item_deposit_box_color", OmegaGUI::color(255, 0, 0) },
				{ Payday3::EItemType::Keycard, "visuals.show_keycards", "visuals.item_keycard_color", OmegaGUI::color(0, 0, 255) },
				{ Payday3::EItemType::QRPhone, "visuals.show_qr_phones", "visuals.item_qr_phone_color", OmegaGUI::color(255, 255, 0) },
				{ Payday3::EItemType::USB, "visuals.show_usbs", "visuals.item_usb_color", OmegaGUI::color(255, 128, 0) },
				{ Payday3::EItemType::KeycardReader, "visuals.show_keycard_readers", "visuals.item_keycard_reader_color", OmegaGUI::color(255, 128, 128) },
			} };

			bool* m_pOutlineColors = nullptr;
			std::array<stOutlineSetting, 5> m_aOutlines = { {
				{ L"ID_2_NonOccluded_Outline", "visuals.outline_interactables", OmegaGUI::color(255, 165, 0, 49) },
				{ L"ID_3_Occuded_Outline", "visuals.outline_marked", OmegaGUI::color(249, 115, 118) },
				{ L"ID_5_Occuded_Outline", "visuals.outline_teammates", OmegaGUI::color(156, 173, 164) },
				{ L"ID_9_Occuded_Outline", "visuals.outline_bags", OmegaGUI::color(184, 177, 145) },
				{ L"ID_10_Occuded_Outline", "visuals.outline_placeables", OmegaGUI::color(255, 150, 0) },
			} };
			std::map<std::wstring_view, SDK::FName> m_mapOutlineNames;
			std::vector<stOutlineSwap> m_aOutlineSwaps;
			std::map<std::pair<SDK::UMaterialInstanceDynamic*, std::wstring_view>, SDK::FLinearColor> m_mapOutlineOriginals;
			SDK::UWorld* m_pOutlineWorld = nullptr;
			SDK::UPostProcessComponent* m_pOutlineComponent = nullptr;
			std::chrono::steady_clock::time_point m_tpNextOutline;
			std::atomic<bool> m_bOutlineQueued = false;
			std::atomic<bool> m_bOutlinesApplied = false;

			std::unordered_map<SDK::AActor*, stOutlined> m_mapOutlined;
			std::chrono::steady_clock::time_point m_tpNextOutlineUpdate;
			std::atomic<bool> m_bOutlineUpdateQueued = false;
			std::atomic<bool> m_bOutlinesActive = false;

			SDK::UWorld* m_pLastWorld = nullptr;
			std::unordered_map<SDK::USkeletalMeshComponent*, BoneCache_t> m_mapBones;
		};

		clVisualsFeature g_clVisualsFeature;
	}
}

#endif
