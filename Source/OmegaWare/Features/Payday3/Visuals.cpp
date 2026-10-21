#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "Config/Config.hpp"
#include "Core/Framework.hpp"
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
#include <cctype>
#include <cmath>
#include <fstream>
#include <mutex>
#include <format>
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

		struct stItemSetting
		{
			Payday3::EItemType eType;
			std::string_view strShow;
			std::string_view strColor;
			OmegaGUI::color colDefault;
			bool* pShow = nullptr;
			OmegaGUI::color* pColor = nullptr;
		};

		class clVisualsFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "visuals"; }

			bool Setup() override
			{
				m_pBox = &Config::Add("visuals.box", false);
				m_stBoxColors = { &Config::Add("visuals.box_cop_color", OmegaGUI::color(255, 0, 0)), &Config::Add("visuals.box_civilian_color", OmegaGUI::color(0, 255, 0)) };
				m_pName = &Config::Add("visuals.name", false);
				m_stNameColors = { &Config::Add("visuals.name_cop_color", OmegaGUI::color(255, 255, 255)), &Config::Add("visuals.name_civilian_color", OmegaGUI::color(0, 0, 255)) };
				m_pDistance = &Config::Add("visuals.distance", false);
				m_stDistanceColors = { &Config::Add("visuals.distance_cop_color", OmegaGUI::color(255, 0, 0)), &Config::Add("visuals.distance_civilian_color", OmegaGUI::color(0, 255, 0)) };
				m_pHealthBar = &Config::Add("visuals.health_bar", false);
				m_stHealthColors = { &Config::Add("visuals.health_bar_cop_color", OmegaGUI::color(0, 255, 0)), &Config::Add("visuals.health_bar_civilian_color", OmegaGUI::color(255, 0, 0)) };
				m_pArmorBar = &Config::Add("visuals.armor_bar", false);
				m_pArmorColor = &Config::Add("visuals.armor_bar_color", OmegaGUI::color(0, 128, 255));
				m_pSkeleton = &Config::Add("visuals.skeleton", false);
				m_stSkeletonColors = { &Config::Add("visuals.skeleton_cop_color", OmegaGUI::color(255, 255, 0)), &Config::Add("visuals.skeleton_civilian_color", OmegaGUI::color(0, 255, 255)) };
				m_pHighlight = &Config::Add("visuals.highlight", false);
				m_pLoot = &Config::Add("visuals.loot", false);
				m_pItems = &Config::Add("visuals.items", false);
				m_pReaders = &Config::Add("visuals.keycard_readers", false);
				m_pDropOffs = &Config::Add("visuals.drop_offs", false);
				m_pDropOffColor = &Config::Add("visuals.drop_off_color", OmegaGUI::color(0, 255, 128));
				m_pShowCops = &Config::Add("visuals.show_cops", true);
				m_pShowCivilians = &Config::Add("visuals.show_civilians", true);
				for (stItemSetting& rSetting : m_aItems)
				{
					if (!rSetting.strShow.empty())
						rSetting.pShow = &Config::Add(rSetting.strShow, true);
					rSetting.pColor = &Config::Add(rSetting.strColor, rSetting.colDefault);
				}
				return true;
			}

			void Render() override
			{
				const bool bAnyEnemy = *m_pBox || *m_pName || *m_pDistance || *m_pHealthBar || *m_pArmorBar || *m_pSkeleton;
				const bool bAnyItems = *m_pLoot || *m_pItems || *m_pReaders || *m_pDropOffs;
				if ((!bAnyEnemy && !bAnyItems) || !Payday3::IsReady())
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
				if (bAnyEnemy)
					DrawCharacters(rList, pController, vecCamera);
				if (bAnyItems)
					DrawItems(rList, pController, pWorld);
			}

			void Run() override
			{
				if (!*m_pHighlight || !Payday3::IsReady() || m_bHighlightQueued.exchange(true))
					return;
				if (!Unreal::RunOnGameThread([this] {
						m_bHighlightQueued = false;
						if (IsActive() && *m_pHighlight)
							MarkCharacters();
					}))
					m_bHighlightQueued = false;
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				OmegaGUI::BeginColumns(2);
				if (OmegaGUI::BeginSection("visuals.section_characters"_l))
				{
					OmegaGUI::Checkbox("visuals.box"_l, m_pBox);
					OmegaGUI::Checkbox("visuals.name"_l, m_pName);
					OmegaGUI::Checkbox("visuals.distance"_l, m_pDistance);
					OmegaGUI::Checkbox("visuals.health_bar"_l, m_pHealthBar);
					OmegaGUI::Checkbox("visuals.armor_bar"_l, m_pArmorBar);
					OmegaGUI::Checkbox("visuals.skeleton"_l, m_pSkeleton);
					OmegaGUI::Checkbox("visuals.highlight"_l, m_pHighlight);
					OmegaGUI::Separator();
					OmegaGUI::Checkbox("visuals.show_cops"_l, m_pShowCops);
					OmegaGUI::Checkbox("visuals.show_civilians"_l, m_pShowCivilians);
					OmegaGUI::EndSection();
				}
				if (OmegaGUI::BeginSection("visuals.section_objects"_l))
				{
					OmegaGUI::Checkbox("visuals.loot"_l, m_pLoot);
					OmegaGUI::Checkbox("visuals.items"_l, m_pItems);
					OmegaGUI::Checkbox("visuals.keycard_readers"_l, m_pReaders);
					OmegaGUI::Checkbox("visuals.drop_offs"_l, m_pDropOffs);
					OmegaGUI::EndSection();
				}
				if (OmegaGUI::BeginSection("visuals.section_debug"_l))
				{
					if (OmegaGUI::Button("visuals.dump_outlines"_l) && Payday3::IsReady() && !m_bDumpQueued.exchange(true))
					{
						if (!Unreal::RunOnGameThread([this] { DumpOutlines(); m_bDumpQueued = false; }))
							m_bDumpQueued = false;
					}
					std::scoped_lock lckDump(m_mtxDump);
					if (!m_strDumpResult.empty())
						OmegaGUI::TextWrapped(m_strDumpResult);
					OmegaGUI::EndSection();
				}
				OmegaGUI::NextColumn();
				if (OmegaGUI::BeginSection("visuals.section_colors"_l))
				{
					if (OmegaGUI::CollapsingHeader(std::format("{}###visuals.group_characters", "visuals.section_characters"_t), true) && OmegaGUI::BeginTable("##visuals.character_colors", 3, OmegaGUI::ETableFlags::RowBackground))
					{
						OmegaGUI::TableSetupColumn({}, OmegaGUI::ETableColumnFlags::None, 1.3f);
						OmegaGUI::TableSetupColumn("visuals.show_cops"_t);
						OmegaGUI::TableSetupColumn("visuals.show_civilians"_t);
						OmegaGUI::TableHeadersRow();
						auto fnRow = [](std::string_view strName, std::string_view strCop, OmegaGUI::color* pCop, std::string_view strCivilian, OmegaGUI::color* pCivilian) {
							OmegaGUI::TableNextRow();
							OmegaGUI::TableNextColumn();
							OmegaGUI::TextUnformatted(OmegaLocalization::Get(strName));
							OmegaGUI::TableNextColumn();
							OmegaGUI::ColorEdit(std::format("###{}", strCop), pCop);
							OmegaGUI::TableNextColumn();
							if (pCivilian)
								OmegaGUI::ColorEdit(std::format("###{}", strCivilian), pCivilian);
						};
						fnRow("visuals.box", "visuals.box_cop_color", m_stBoxColors.pCop, "visuals.box_civilian_color", m_stBoxColors.pCivilian);
						fnRow("visuals.name", "visuals.name_cop_color", m_stNameColors.pCop, "visuals.name_civilian_color", m_stNameColors.pCivilian);
						fnRow("visuals.distance", "visuals.distance_cop_color", m_stDistanceColors.pCop, "visuals.distance_civilian_color", m_stDistanceColors.pCivilian);
						fnRow("visuals.health_bar", "visuals.health_bar_cop_color", m_stHealthColors.pCop, "visuals.health_bar_civilian_color", m_stHealthColors.pCivilian);
						fnRow("visuals.armor_bar", "visuals.armor_bar_color", m_pArmorColor, {}, nullptr);
						fnRow("visuals.skeleton", "visuals.skeleton_cop_color", m_stSkeletonColors.pCop, "visuals.skeleton_civilian_color", m_stSkeletonColors.pCivilian);
						OmegaGUI::EndTable();
					}
					auto fnItemGroup = [&](std::string_view strHeader, std::string_view strID, const bool* pCategory) {
						if (!OmegaGUI::CollapsingHeader(std::format("{}###visuals.group_{}", OmegaLocalization::Get(strHeader), strID), true) || !OmegaGUI::BeginTable(std::format("##visuals.{}_colors", strID), 2, OmegaGUI::ETableFlags::RowBackground))
							return;
						OmegaGUI::TableSetupColumn({}, OmegaGUI::ETableColumnFlags::None, 1.3f);
						OmegaGUI::TableSetupColumn({}, OmegaGUI::ETableColumnFlags::None, 1.f);
						for (const stItemSetting& rSetting : m_aItems)
						{
							if (CategoryToggle(rSetting.eType) != pCategory)
								continue;
							OmegaGUI::TableNextRow();
							OmegaGUI::TableNextColumn();
							if (rSetting.pShow)
								OmegaGUI::Checkbox(std::format("{}###{}", OmegaLocalization::Get(rSetting.strShow), rSetting.strShow), rSetting.pShow);
							else
								OmegaGUI::TextUnformatted(OmegaLocalization::Get(rSetting.strColor));
							OmegaGUI::TableNextColumn();
							OmegaGUI::ColorEdit(std::format("###{}", rSetting.strColor), rSetting.pColor);
						}
						if (pCategory == m_pReaders)
						{
							OmegaGUI::TableNextRow();
							OmegaGUI::TableNextColumn();
							OmegaGUI::TextUnformatted("visuals.drop_off_color"_t);
							OmegaGUI::TableNextColumn();
							OmegaGUI::ColorEdit("###visuals.drop_off_color", m_pDropOffColor);
						}
						OmegaGUI::EndTable();
					};
					fnItemGroup("visuals.loot", "loot", m_pLoot);
					fnItemGroup("visuals.items", "items", m_pItems);
					fnItemGroup("visuals.group_objectives", "objectives", m_pReaders);
					OmegaGUI::EndSection();
				}
				OmegaGUI::EndColumns();
			}

		private:
			void DumpOutlines()
			{
				const std::filesystem::path pathDump = DataDirectory() / "Logs" / "OutlineDump.txt";
				std::ofstream fileDump(pathDump, std::ios::trunc);
				if (!fileDump)
				{
					LogError("could not open {} for the outline dump", pathDump.string());
					return;
				}

				auto fnLower = [](std::string strText) {
					std::ranges::transform(strText, strText.begin(), [](unsigned char chLetter) { return static_cast<char>(std::tolower(chLetter)); });
					return strText;
				};
				auto fnMatches = [&](const std::string& strName) {
					const std::string strLower = fnLower(strName);
					for (std::string_view strKey : { "outline", "highlight", "mark", "glow", "fresnel" })
						if (strLower.find(strKey) != std::string::npos)
							return true;
					return false;
				};
				auto fnColor = [](const SDK::FLinearColor& stColor) {
					return std::format("({:.3f}, {:.3f}, {:.3f}, {:.3f})", stColor.R, stColor.G, stColor.B, stColor.A);
				};

				int iAssets = 0;
				int iMaterials = 0;
				int iCollections = 0;
				SDK::TUObjectArray& rObjects = *SDK::UObject::GObjects;
				for (int iObject = 0; iObject < rObjects.Num(); ++iObject)
				{
					SDK::UObject* pObject = rObjects.GetByIndex(iObject);
					if (!Payday3::IsAlive(pObject) || !Payday3::IsAlive(pObject->Class))
						continue;
					const std::string strName = pObject->GetName();
					if (strName.starts_with("Default__"))
						continue;

					if (pObject->IsA(SDK::USBZOutlineAsset::StaticClass()))
					{
						const auto* pAsset = static_cast<SDK::USBZOutlineAsset*>(pObject);
						fileDump << std::format("outline asset {}: color index {}, priority {}, distance {:.1f}\n", pObject->GetFullName(), static_cast<int>(pAsset->ColorIndex), pAsset->Priority, pAsset->Distance);
						++iAssets;
					}
					else if (pObject->IsA(SDK::UMaterialParameterCollection::StaticClass()))
					{
						auto* pCollection = static_cast<SDK::UMaterialParameterCollection*>(pObject);
						fileDump << std::format("parameter collection {}\n", pObject->GetFullName());
						for (int iParameter = 0; iParameter < pCollection->VectorParameters.Num(); ++iParameter)
							fileDump << std::format("  vector {} = {}\n", pCollection->VectorParameters[iParameter].ParameterName.ToString(), fnColor(pCollection->VectorParameters[iParameter].DefaultValue));
						for (int iParameter = 0; iParameter < pCollection->ScalarParameters.Num(); ++iParameter)
							fileDump << std::format("  scalar {} = {:.3f}\n", pCollection->ScalarParameters[iParameter].ParameterName.ToString(), pCollection->ScalarParameters[iParameter].DefaultValue);
						++iCollections;
					}
					else if (pObject->IsA(SDK::UMaterialInstance::StaticClass()) && fnMatches(strName))
					{
						auto* pInstance = static_cast<SDK::UMaterialInstance*>(pObject);
						fileDump << std::format("material instance {} (parent {})\n", pObject->GetFullName(), Payday3::IsAlive(pInstance->Parent) ? pInstance->Parent->GetFullName() : std::string("none"));
						for (int iParameter = 0; iParameter < pInstance->VectorParameterValues.Num(); ++iParameter)
							fileDump << std::format("  vector {} = {}\n", pInstance->VectorParameterValues[iParameter].ParameterInfo.Name.ToString(), fnColor(pInstance->VectorParameterValues[iParameter].ParameterValue));
						for (int iParameter = 0; iParameter < pInstance->ScalarParameterValues.Num(); ++iParameter)
							fileDump << std::format("  scalar {} = {:.3f}\n", pInstance->ScalarParameterValues[iParameter].ParameterInfo.Name.ToString(), pInstance->ScalarParameterValues[iParameter].ParameterValue);
						++iMaterials;
					}
					else if (pObject->IsA(SDK::UMaterial::StaticClass()) && fnMatches(strName))
					{
						fileDump << std::format("material {} (base material, parameters live in its expressions)\n", pObject->GetFullName());
						++iMaterials;
					}
				}
				fileDump.flush();
				LogInfo("outline dump: {} outline assets, {} materials, {} parameter collections written to {}", iAssets, iMaterials, iCollections, pathDump.string());
				std::scoped_lock lckDump(m_mtxDump);
				m_strDumpResult = OmegaLocalization::Format("visuals.dump_done", iAssets, iMaterials, iCollections, pathDump.string());
			}

			void MarkCharacters() const
			{
				SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns();
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				if (!pPawns || !pController)
					return;
				SDK::APawn* pLocalPawn = pController->AcknowledgedPawn;
				SDK::TArray<SDK::UObject*>& aActors = pPawns->Objects;
				for (int iActor = 0; iActor < aActors.Num(); ++iActor)
				{
					auto* pActor = static_cast<SDK::AActor*>(aActors[iActor]);
					if (!Memory::IsValidObjectPtr(pActor) || pActor == pLocalPawn)
						continue;
					auto* pCharacter = static_cast<SDK::ASBZCharacter*>(pActor);
					if (!pCharacter->bIsAlive)
						continue;
					const Payday3::EEnemyCategory eCategory = Payday3::GetEnemyInfo(Payday3::ResolveEnemyType(pActor)).eCategory;
					if ((eCategory == Payday3::EEnemyCategory::Cop && *m_pShowCops) || (eCategory == Payday3::EEnemyCategory::Civilian && *m_pShowCivilians))
						pCharacter->Multicast_SetMarked(true);
				}
			}

			void DrawCharacters(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, const SDK::FVector& vecCamera)
			{
				SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns();
				if (!pPawns)
					return;

				const bool bNeedsBox = *m_pBox || *m_pName || *m_pDistance || *m_pHealthBar || *m_pArmorBar;
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
					const bool bCop = stInfo.eCategory == Payday3::EEnemyCategory::Cop;
					const bool bCivilian = stInfo.eCategory == Payday3::EEnemyCategory::Civilian;
					if ((!bCop && !bCivilian) || (bCop && !*m_pShowCops) || (bCivilian && !*m_pShowCivilians))
						continue;

					if (bNeedsBox)
					{
						const std::optional<stBox> optBox = CalculateBox(pController, pActor, pCharacter->Mesh, vecCamera);
						if (!optBox)
							continue;
						const stPawnStats stStats = *m_pHealthBar || *m_pArmorBar ? ReadPawnStats(pCharacter).value_or(stPawnStats{}) : stPawnStats{};
						const float flDistance = static_cast<float>((pActor->K2_GetActorLocation() - vecCamera).Magnitude() / 100.0);
						DrawBox(rList, *optBox, stInfo.strName, flDistance, stStats, bCop);
					}

					if (*m_pSkeleton)
						DrawSkeleton(rList, pController, pCharacter->Mesh, m_stSkeletonColors.Get(bCop));

				}
			}

			void DrawBox(OmegaGUI::clDrawList& rList, const stBox& stRect, std::string_view strName, float flDistance, const stPawnStats& stStats, bool bCop) const
			{
				const float flBoxWidth = stRect.flRight - stRect.flLeft;
				const float flBoxHeight = stRect.flBottom - stRect.flTop;
				if (!std::isfinite(flBoxWidth) || !std::isfinite(flBoxHeight) || flBoxWidth <= 0.f || flBoxHeight <= 0.f)
					return;

				if (*m_pBox)
				{
					rList.AddRect({ stRect.flLeft - 1.f, stRect.flTop - 1.f }, { stRect.flRight + 1.f, stRect.flBottom + 1.f }, kOutline, 0.f, 1.6f);
					rList.AddRect({ stRect.flLeft, stRect.flTop }, { stRect.flRight, stRect.flBottom }, m_stBoxColors.Get(bCop), 0.f, 1.5f);
				}

				if (*m_pName || *m_pDistance)
				{
					const std::string strNameText = *m_pName ? FormatEntityName(strName) : std::string();
					const std::string strDistanceText = *m_pDistance ? FormatDistance(flDistance) : std::string();
					const OmegaGUI::vec2 vecNameSize = OmegaGUI::CalcTextSize(strNameText);
					const OmegaGUI::vec2 vecDistanceSize = OmegaGUI::CalcTextSize(strDistanceText);
					const float flSpace = !strNameText.empty() && !strDistanceText.empty() ? OmegaGUI::CalcTextSize(" ").x : 0.f;
					const float flLineHeight = std::max(vecNameSize.y, vecDistanceSize.y);
					OmegaGUI::vec2 vecText{ stRect.flLeft + (flBoxWidth - (vecNameSize.x + flSpace + vecDistanceSize.x)) * .5f, stRect.flTop - flLineHeight - 4.f };
					if (!strNameText.empty())
					{
						DrawOutlinedText(rList, vecText, m_stNameColors.Get(bCop), strNameText);
						vecText.x += vecNameSize.x + flSpace;
					}
					if (!strDistanceText.empty())
						DrawOutlinedText(rList, vecText, m_stDistanceColors.Get(bCop), strDistanceText);
				}

				constexpr float flBarWidth = 4.f;
				if (*m_pHealthBar)
				{
					const float flFraction = stStats.flHealthMax > 0.f ? std::clamp(stStats.flHealth / stStats.flHealthMax, 0.f, 1.f) : 0.f;
					rList.AddRectFilled({ stRect.flLeft - flBarWidth - 3.f, stRect.flTop }, { stRect.flLeft - 1.f, stRect.flBottom }, kOutline);
					rList.AddRectFilled({ stRect.flLeft - flBarWidth - 2.f, stRect.flBottom - flBoxHeight * flFraction }, { stRect.flLeft - 2.f, stRect.flBottom }, m_stHealthColors.Get(bCop));
				}

				if (*m_pArmorBar)
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

			[[nodiscard]] const stItemSetting* Setting(Payday3::EItemType eType) const
			{
				const auto itSetting = std::ranges::find(m_aItems, eType, &stItemSetting::eType);
				return itSetting != m_aItems.end() ? &*itSetting : nullptr;
			}

			[[nodiscard]] const bool* CategoryToggle(Payday3::EItemType eType) const
			{
				switch (eType)
				{
				case Payday3::EItemType::Cash:
				case Payday3::EItemType::MoneyMedium:
				case Payday3::EItemType::MoneyLarge:
				case Payday3::EItemType::Jewelry:
					return m_pLoot;
				case Payday3::EItemType::KeycardReader:
					return m_pReaders;
				default:
					return m_pItems;
				}
			}

			void DrawDropOff(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::ASBZBagTriggerVolume* pVolume) const
			{
				const std::optional<OmegaGUI::vec2> optScreen = Project(pController, pVolume->K2_GetActorLocation());
				if (!optScreen)
					return;
				const bool bActive = pVolume->GetActorEnableCollision();
				const std::string_view strLabel = bActive ? "Drop-off" : "Drop-off (inactive)";
				const OmegaGUI::color col = bActive ? *m_pDropOffColor : m_pDropOffColor->WithAlpha(110);
				const OmegaGUI::vec2 vecSize = OmegaGUI::CalcTextSize(strLabel);
				rList.AddCircle(*optScreen, 6.f, kOutline, 16, 3.f);
				rList.AddCircle(*optScreen, 6.f, col, 16, 1.5f);
				DrawOutlinedText(rList, *optScreen - OmegaGUI::vec2{ vecSize.x * .5f, vecSize.y + 10.f }, col, strLabel);
			}

			void DrawItems(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::UWorld* pWorld)
			{
				SDK::TArray<SDK::ULevel*>& aLevels = pWorld->Levels;
				for (int iLevel = 0; iLevel < aLevels.Num(); ++iLevel)
				{
					SDK::ULevel* pLevel = aLevels[iLevel];
					if (!Memory::IsValidObjectPtr(pLevel))
						continue;
					for (int iActor = 0; iActor < pLevel->Actors.Num(); ++iActor)
					{
						SDK::AActor* pActor = pLevel->Actors[iActor];
						if (!Memory::IsValidObjectPtr(pActor))
							continue;

						if (*m_pDropOffs)
						{
							if (SDK::ASBZBagTriggerVolume* pVolume = Payday3::AsDropOff(pActor))
							{
								DrawDropOff(rList, pController, pVolume);
								continue;
							}
						}

						const Payday3::stItemInfo& rItem = Payday3::ResolveItem(pActor);
						const stItemSetting* pSetting = Setting(rItem.eType);
						if (!pSetting || !*CategoryToggle(rItem.eType) || (pSetting->pShow && !*pSetting->pShow))
							continue;

						const std::optional<OmegaGUI::vec2> optScreen = Project(pController, pActor->K2_GetActorLocation());
						if (!optScreen)
							continue;
						const OmegaGUI::vec2 vecSize = OmegaGUI::CalcTextSize(rItem.strLabel);
						DrawOutlinedText(rList, *optScreen - vecSize * .5f, *pSetting->pColor, rItem.strLabel);
					}
				}
			}

			bool* m_pBox = nullptr;
			stColorPair m_stBoxColors;
			bool* m_pName = nullptr;
			stColorPair m_stNameColors;
			bool* m_pDistance = nullptr;
			stColorPair m_stDistanceColors;
			bool* m_pHealthBar = nullptr;
			stColorPair m_stHealthColors;
			bool* m_pArmorBar = nullptr;
			OmegaGUI::color* m_pArmorColor = nullptr;
			bool* m_pSkeleton = nullptr;
			stColorPair m_stSkeletonColors;
			bool* m_pHighlight = nullptr;
			bool* m_pLoot = nullptr;
			bool* m_pItems = nullptr;
			bool* m_pReaders = nullptr;
			bool* m_pDropOffs = nullptr;
			OmegaGUI::color* m_pDropOffColor = nullptr;
			bool* m_pShowCops = nullptr;
			bool* m_pShowCivilians = nullptr;
			std::array<stItemSetting, 9> m_aItems = { {
				{ Payday3::EItemType::Cash, "visuals.show_cash", "visuals.item_cash_color", OmegaGUI::color(0, 255, 255) },
				{ Payday3::EItemType::MoneyMedium, "visuals.show_money_medium", "visuals.item_money_medium_color", OmegaGUI::color(120, 220, 120) },
				{ Payday3::EItemType::MoneyLarge, "visuals.show_money_large", "visuals.item_money_large_color", OmegaGUI::color(0, 255, 0) },
				{ Payday3::EItemType::Jewelry, "visuals.show_jewelry", "visuals.item_jewelry_color", OmegaGUI::color(255, 0, 255) },
				{ Payday3::EItemType::DepositBox, "visuals.show_deposit_boxes", "visuals.item_deposit_box_color", OmegaGUI::color(255, 0, 0) },
				{ Payday3::EItemType::Keycard, "visuals.show_keycards", "visuals.item_keycard_color", OmegaGUI::color(0, 0, 255) },
				{ Payday3::EItemType::QRPhone, "visuals.show_qr_phones", "visuals.item_qr_phone_color", OmegaGUI::color(255, 255, 0) },
				{ Payday3::EItemType::USB, "visuals.show_usbs", "visuals.item_usb_color", OmegaGUI::color(255, 128, 0) },
				{ Payday3::EItemType::KeycardReader, {}, "visuals.item_keycard_reader_color", OmegaGUI::color(255, 128, 128) },
			} };

			SDK::UWorld* m_pLastWorld = nullptr;
			std::atomic<bool> m_bHighlightQueued = false;
			std::atomic<bool> m_bDumpQueued = false;
			std::mutex m_mtxDump;
			std::string m_strDumpResult;
			std::unordered_map<SDK::USkeletalMeshComponent*, BoneCache_t> m_mapBones;
		};

		clVisualsFeature g_clVisualsFeature;
	}
}

#endif
