#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#pragma warning(push)
#pragma warning(disable : 4005 4369 4309 4267 4244)
#include "SDK/BP_MethIngredientBase_classes.hpp"
#include "SDK/BP_QRPhone_classes.hpp"
#include "SDK/BP_USBDrive_classes.hpp"
#include "SDK/BP_WoodenCrate_classes.hpp"
#include "SDK/BP_WoodenCrate_SatellitePart_LowValue_classes.hpp"
#pragma warning(pop)

#include "SDK/Starbreeze_parameters.hpp"

#include "Config/Config.hpp"
#include "Core/Log.hpp"
#include "Core/Profiler.hpp"
#include "Engines/Unreal/Field.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Engines/Unreal/RpcValidate.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#include "OmegaRenderHook/Core/Guarded.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
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

		struct BoneCache_t
		{
			std::int32_t iIndex = -1;
			std::array<int, BoneCount> aIndices;
			std::array<SDK::FName, BoneCount> aNames;
		};

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

		enum class EColorSet : std::uint8_t
		{
			Cop,
			Civilian,
			Player,
			Vip,
		};

		struct stColorPair
		{
			OmegaGUI::color* pCop = nullptr;
			OmegaGUI::color* pCivilian = nullptr;
			OmegaGUI::color* pPlayer = nullptr;
			OmegaGUI::color* pVip = nullptr;

			[[nodiscard]] OmegaGUI::color Get(EColorSet eSet) const
			{
				switch (eSet)
				{
				case EColorSet::Cop: return *pCop;
				case EColorSet::Civilian: return *pCivilian;
				case EColorSet::Vip: return *pVip;
				default: return *pPlayer;
				}
			}
		};

		[[nodiscard]] bool IsValidUtf8Text(std::string_view strText)
		{
			for (std::size_t uIndex = 0; uIndex < strText.size();)
			{
				const auto byLead = static_cast<unsigned char>(strText[uIndex]);
				if (byLead < 32 || byLead == 127)
					return false;
				std::size_t uLength = 1;
				if (byLead >= 0xF0 && byLead <= 0xF4)
					uLength = 4;
				else if (byLead >= 0xE0)
					uLength = 3;
				else if (byLead >= 0xC2)
					uLength = 2;
				else if (byLead >= 0x80)
					return false;
				if (uIndex + uLength > strText.size())
					return false;
				for (std::size_t uTail = 1; uTail < uLength; ++uTail)
					if ((static_cast<unsigned char>(strText[uIndex + uTail]) & 0xC0) != 0x80)
						return false;
				uIndex += uLength;
			}
			return true;
		}

		std::string FormatEntityName(std::string_view strName)
		{
			if (strName.empty() || strName.size() >= 256)
				return "Unknown";
			if (!IsValidUtf8Text(strName))
				return "Invalid";
			return std::string(strName);
		}

		struct stEspTextHash
		{
			using is_transparent = void;
			std::size_t operator()(std::string_view strText) const { return std::hash<std::string_view>{}(strText); }
			std::size_t operator()(const std::string& strText) const { return std::hash<std::string_view>{}(strText); }
		};

		const std::string& EspText(std::string_view strEnglish)
		{
			thread_local std::string t_strLocale;
			thread_local std::unordered_map<std::string, std::string, stEspTextHash, std::equal_to<>> t_mapText;
			const std::string_view strLocale = OmegaLocalization::GetLocale();
			if (strLocale != t_strLocale)
			{
				t_strLocale = std::string(strLocale);
				t_mapText.clear();
			}
			if (const auto itText = t_mapText.find(strEnglish); itText != t_mapText.end())
				return itText->second;
			std::string strKey = "esp.";
			bool bSeparator = false;
			for (const char chLetter : strEnglish)
			{
				const bool bAlnum = (chLetter >= 'a' && chLetter <= 'z') || (chLetter >= 'A' && chLetter <= 'Z') || (chLetter >= '0' && chLetter <= '9');
				if (bAlnum)
				{
					if (bSeparator)
						strKey.push_back('_');
					strKey.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(chLetter))));
					bSeparator = false;
				}
				else if (strKey.size() > 4)
					bSeparator = true;
			}
			if (t_mapText.size() > 1024)
				t_mapText.clear();
			const std::string strText = OmegaLocalization::Has(strKey) ? std::string(OmegaLocalization::Get(strKey)) : std::string(strEnglish);
			return t_mapText.emplace(std::string(strEnglish), strText).first->second;
		}

		std::string FormatDistance(float flDistance)
		{
			return flDistance >= 0.f && flDistance < 10000.f ? std::format("[{:.0f}m]", flDistance) : "[???m]";
		}

		std::optional<OmegaGUI::vec2> ProjectEngine(SDK::APlayerController* pController, const SDK::FVector& vecWorld)
		{
			SDK::FVector2D vecScreen;
			if (!pController->ProjectWorldLocationToScreen(vecWorld, &vecScreen, false))
				return std::nullopt;
			return OmegaGUI::vec2{ static_cast<float>(vecScreen.X), static_cast<float>(vecScreen.Y) };
		}

		struct stProjector
		{
			struct stAxis
			{
				double x = 0.;
				double y = 0.;
				double z = 0.;

				[[nodiscard]] double Dot(const stAxis& rOther) const { return x * rOther.x + y * rOther.y + z * rOther.z; }
			};

			bool bPrepared = false;
			bool bValid = false;
			stAxis vecOrigin;
			stAxis vecForward;
			stAxis vecRight;
			stAxis vecUp;
			double dCenterX = 0.;
			double dCenterY = 0.;
			double dFocalX = 0.;
			double dFocalY = 0.;

			[[nodiscard]] SDK::FVector Point(double dForward, double dRight, double dUp) const
			{
				return SDK::FVector(vecOrigin.x + vecForward.x * dForward + vecRight.x * dRight + vecUp.x * dUp, vecOrigin.y + vecForward.y * dForward + vecRight.y * dRight + vecUp.y * dUp, vecOrigin.z + vecForward.z * dForward + vecRight.z * dRight + vecUp.z * dUp);
			}

			[[nodiscard]] std::optional<OmegaGUI::vec2> ProjectOwn(const SDK::FVector& vecWorld) const
			{
				const stAxis vecDelta{ vecWorld.X - vecOrigin.x, vecWorld.Y - vecOrigin.y, vecWorld.Z - vecOrigin.z };
				const double dDepth = vecDelta.Dot(vecForward);
				if (dDepth <= 0.)
					return std::nullopt;
				return OmegaGUI::vec2{ static_cast<float>(dCenterX + dFocalX * vecDelta.Dot(vecRight) / dDepth), static_cast<float>(dCenterY - dFocalY * vecDelta.Dot(vecUp) / dDepth) };
			}

			void Prepare(SDK::APlayerController* pController)
			{
				bPrepared = false;
				bValid = false;
				SDK::APlayerCameraManager* pCamera = pController->PlayerCameraManager;
				if (!Memory::IsValidObjectPtr(pCamera))
					return;
				const SDK::FMinimalViewInfo& rView = pCamera->CameraCachePrivate.POV;
				vecOrigin = { rView.Location.X, rView.Location.Y, rView.Location.Z };
				constexpr double kDegrees = std::numbers::pi / 180.0;
				const double dSinPitch = std::sin(rView.Rotation.Pitch * kDegrees);
				const double dCosPitch = std::cos(rView.Rotation.Pitch * kDegrees);
				const double dSinYaw = std::sin(rView.Rotation.Yaw * kDegrees);
				const double dCosYaw = std::cos(rView.Rotation.Yaw * kDegrees);
				const double dSinRoll = std::sin(rView.Rotation.Roll * kDegrees);
				const double dCosRoll = std::cos(rView.Rotation.Roll * kDegrees);
				vecForward = { dCosPitch * dCosYaw, dCosPitch * dSinYaw, dSinPitch };
				vecRight = { dSinRoll * dSinPitch * dCosYaw - dCosRoll * dSinYaw, dSinRoll * dSinPitch * dSinYaw + dCosRoll * dCosYaw, -dSinRoll * dCosPitch };
				vecUp = { -(dCosRoll * dSinPitch * dCosYaw + dSinRoll * dSinYaw), dCosYaw * dSinRoll - dCosRoll * dSinPitch * dSinYaw, dCosRoll * dCosPitch };
				bPrepared = true;

				constexpr double kDepth = 1000.;
				constexpr double kSide = 400.;
				constexpr double kRise = 300.;
				const std::optional<OmegaGUI::vec2> optFirst = ProjectEngine(pController, Point(kDepth, kSide, kRise));
				const std::optional<OmegaGUI::vec2> optSecond = ProjectEngine(pController, Point(kDepth, -kSide, -kRise));
				if (!optFirst || !optSecond)
					return;
				dFocalX = (static_cast<double>(optFirst->x) - optSecond->x) / (2. * kSide / kDepth);
				dFocalY = (static_cast<double>(optSecond->y) - optFirst->y) / (2. * kRise / kDepth);
				dCenterX = (static_cast<double>(optFirst->x) + optSecond->x) * .5;
				dCenterY = (static_cast<double>(optFirst->y) + optSecond->y) * .5;
				if (!std::isfinite(dFocalX) || !std::isfinite(dFocalY) || dFocalX <= 1. || dFocalY <= 1.)
					return;

				constexpr float kTolerance = 1.5f;
				const SDK::FVector vecCheck = Point(1700., -650., 180.);
				const std::optional<OmegaGUI::vec2> optExpected = ProjectEngine(pController, vecCheck);
				const std::optional<OmegaGUI::vec2> optOwn = ProjectOwn(vecCheck);
				bValid = optExpected && optOwn && std::abs(optExpected->x - optOwn->x) <= kTolerance && std::abs(optExpected->y - optOwn->y) <= kTolerance;
			}

			[[nodiscard]] SDK::FVector Origin() const { return SDK::FVector(vecOrigin.x, vecOrigin.y, vecOrigin.z); }
		};

		stProjector g_stProjector;

		std::optional<OmegaGUI::vec2> Project(SDK::APlayerController* pController, const SDK::FVector& vecWorld)
		{
			if (g_stProjector.bValid)
				return g_stProjector.ProjectOwn(vecWorld);
			return ProjectEngine(pController, vecWorld);
		}

		std::optional<stPawnStats> ReadPawnStats(SDK::AActor* pActor)
		{
			if (Payday3::IsDrone(pActor))
			{
				SDK::USBZAIDroneAttributeSet* pDroneSet = OW_FIELD(static_cast<SDK::ASBZAIDrone*>(pActor), AttributeSet);
				if (!Memory::IsValidObjectPtr(pDroneSet))
					return std::nullopt;
				return stPawnStats{ pDroneSet->Health.CurrentValue, pDroneSet->HealthMax.CurrentValue, pDroneSet->Armor.CurrentValue, pDroneSet->ArmorMax.CurrentValue };
			}
			auto* pCharacter = static_cast<SDK::ASBZCharacter*>(pActor);
			SDK::USBZAbilitySystemComponent* pAbilitySystem = OW_FIELD(pCharacter, AbilitySystem);
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

		std::optional<stBox> CalculateActorBox(SDK::APlayerController* pController, SDK::AActor* pActor)
		{
			SDK::FVector vecOrigin;
			SDK::FVector vecExtent;
			pActor->GetActorBounds(true, &vecOrigin, &vecExtent, false);
			stBox stResult{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
			for (int iCorner = 0; iCorner < 8; ++iCorner)
			{
				const SDK::FVector vecCorner(vecOrigin.X + ((iCorner & 1) ? vecExtent.X : -vecExtent.X), vecOrigin.Y + ((iCorner & 2) ? vecExtent.Y : -vecExtent.Y), vecOrigin.Z + ((iCorner & 4) ? vecExtent.Z : -vecExtent.Z));
				const std::optional<OmegaGUI::vec2> optScreen = Project(pController, vecCorner);
				if (!optScreen)
					return std::nullopt;
				stResult.flLeft = std::min(stResult.flLeft, optScreen->x);
				stResult.flTop = std::min(stResult.flTop, optScreen->y);
				stResult.flRight = std::max(stResult.flRight, optScreen->x);
				stResult.flBottom = std::max(stResult.flBottom, optScreen->y);
			}
			return stResult;
		}

		std::optional<stBox> CalculateBox(SDK::APlayerController* pController, SDK::AActor* pActor, SDK::USkeletalMeshComponent* pMesh, const SDK::FVector& vecCamera, const SDK::FVector& vecActor)
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
			const SDK::FVector vecToActor = (vecActor - vecCamera).GetNormalized();
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
			Players,
			Loot,
			KeyItems,
			Objectives,
			Supplies,
		};

		struct stCategory
		{
			std::string_view strKey;
			std::string_view strTab;
			bool bPeople = false;
			bool* pEnabled = nullptr;
			bool* pBox = nullptr;
			bool* pName = nullptr;
			bool* pDistance = nullptr;
			bool* pHealthBar = nullptr;
			bool* pArmorBar = nullptr;
			bool* pSkeleton = nullptr;
			bool* pOutline = nullptr;

			[[nodiscard]] static bool On(const bool* pValue) { return pValue && *pValue; }
			[[nodiscard]] bool AnyDrawn() const { return On(pEnabled) && (On(pBox) || On(pName) || On(pDistance) || On(pHealthBar) || On(pArmorBar) || On(pSkeleton)); }
			[[nodiscard]] bool OutlineOn() const { return On(pEnabled) && On(pOutline); }
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

		enum class EThingKind : std::uint8_t
		{
			Camera,
			DropOff,
			Item,
		};

		struct stThing
		{
			SDK::AActor* pActor = nullptr;
			SDK::UClass* pClass = nullptr;
			EThingKind eKind = EThingKind::Item;
			Payday3::stItemInfo stItem;
			std::string strLabel;
		};

		struct stThingSnapshot
		{
			SDK::UWorld* pWorld = nullptr;
			std::vector<stThing> aThings;
		};

		constexpr std::chrono::milliseconds kThingScanInterval{ 1000 };

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
				m_hKeyItems = Unreal::OnFunction<SDK::Params::PD3KeyItemContainer_OnKeyItemCountChanged>("Starbreeze.PD3KeyItemContainer.OnKeyItemCountChanged",
					[this](SDK::UObject*, SDK::Params::PD3KeyItemContainer_OnKeyItemCountChanged& rParams, bool&) {
						const SDK::FSBZKeyItemCountChangedEvent& rEvent = rParams.KeyItemCountChangedEventData;
						SDK::APlayerController* pController = Unreal::GetPlayerController();
						if (!Payday3::IsAlive(pController) || !pController->PlayerState || rEvent.PlayerState != pController->PlayerState)
							return;
						std::string strKey = rEvent.KeyTagName.ToString() + " " + (Payday3::IsAlive(rEvent.KeyItem) ? rEvent.KeyItem->GetName() : std::string());
						std::ranges::transform(strKey, strKey.begin(), [](unsigned char chLetter) { return static_cast<char>(std::tolower(chLetter)); });
						std::scoped_lock lckHeld(m_mtxHeldKeys);
						if (rEvent.NewValue > 0)
						{
							if (m_mapHeldKeys.emplace(strKey, rEvent.NewValue).second)
								LogInfo("visuals: you now hold the key item \"{}\"", strKey);
							else
								m_mapHeldKeys[strKey] = rEvent.NewValue;
						}
						else
							m_mapHeldKeys.erase(strKey);
					});
				for (stCategory& rCategory : m_aCategories)
				{
					auto fnAdd = [&](std::string_view strOption) { return &Config::Add(std::format("visuals.{}.{}", rCategory.strKey, strOption), false); };
					rCategory.pEnabled = &Config::Add(std::format("visuals.{}.enabled", rCategory.strKey), true);
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
				Category(ECategory::Players).pArmorBar = &Config::Add("visuals.players.armor_bar", false);
				m_pCameras = &Config::Add("visuals.enemies.cameras", false);
				m_pOmegaTag = &Config::Add("visuals.players.omegaware_tag", true);
				m_pAimbotTarget = &Config::Add("visuals.enemies.aimbot_target", true);
				m_pAimbotTargetColor = &Config::Add("visuals.aimbot_target_color", OmegaGUI::color(255, 0, 255));

				m_stBoxColors = { &Config::Add("visuals.box_cop_color", OmegaGUI::color(255, 0, 0)), &Config::Add("visuals.box_civilian_color", OmegaGUI::color(0, 255, 0)), &Config::Add("visuals.box_player_color", OmegaGUI::color(0, 255, 255)) };
				m_stNameColors = { &Config::Add("visuals.name_cop_color", OmegaGUI::color(255, 255, 255)), &Config::Add("visuals.name_civilian_color", OmegaGUI::color(0, 0, 255)), &Config::Add("visuals.name_player_color", OmegaGUI::color(255, 255, 255)) };
				m_stDistanceColors = { &Config::Add("visuals.distance_cop_color", OmegaGUI::color(255, 0, 0)), &Config::Add("visuals.distance_civilian_color", OmegaGUI::color(0, 255, 0)), &Config::Add("visuals.distance_player_color", OmegaGUI::color(0, 255, 255)) };
				m_stHealthColors = { &Config::Add("visuals.health_bar_cop_color", OmegaGUI::color(0, 255, 0)), &Config::Add("visuals.health_bar_civilian_color", OmegaGUI::color(255, 0, 0)), &Config::Add("visuals.health_bar_player_color", OmegaGUI::color(0, 255, 0)) };
				m_pArmorColor = &Config::Add("visuals.armor_bar_color", OmegaGUI::color(0, 128, 255));
				m_stSkeletonColors = { &Config::Add("visuals.skeleton_cop_color", OmegaGUI::color(255, 255, 0)), &Config::Add("visuals.skeleton_civilian_color", OmegaGUI::color(0, 255, 255)), &Config::Add("visuals.skeleton_player_color", OmegaGUI::color(0, 255, 255)) };
				m_pShowDropOffs = &Config::Add("visuals.show_drop_offs", true);
				m_pShowVips = &Config::Add("visuals.show_vips", true);
				m_pVipColor = &Config::Add("visuals.vip_color", OmegaGUI::color(255, 215, 0));
				for (stColorPair* pPair : { &m_stBoxColors, &m_stNameColors, &m_stDistanceColors, &m_stHealthColors, &m_stSkeletonColors })
					pPair->pVip = m_pVipColor;
				m_stVip.pEnabled = m_pShowVips;
				m_stVip.pBox = &Config::Add("visuals.vip.box", true);
				m_stVip.pName = &Config::Add("visuals.vip.name", true);
				m_stVip.pDistance = &Config::Add("visuals.vip.distance", true);
				m_stVip.pOutline = &Config::Add("visuals.vip.outline", false);
#ifdef _DEBUG
				m_pDebugEnabled = &Config::Add("visuals.debug.enabled", false);
				m_pDebugActors = &Config::Add("visuals.debug.actors", true);
				m_pDebugHierarchy = &Config::Add("visuals.debug.hierarchy", true);
				m_pDebugActorDistance = &Config::Add("visuals.debug.actor_distance", 10.f);
				m_pDebugBones = &Config::Add("visuals.debug.bones", false);
				m_pDebugBoneLines = &Config::Add("visuals.debug.bone_lines", true);
				m_pDebugBoneIndices = &Config::Add("visuals.debug.bone_indices", false);
				m_pDebugBoneNames = &Config::Add("visuals.debug.bone_names", false);
				m_pDebugBoneDistance = &Config::Add("visuals.debug.bone_distance", 30.f);
#endif
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
				if (m_hKeyItems != Unreal::kInvalidHook)
					Unreal::RemoveProcessEvent(m_hKeyItems);
				m_hKeyItems = Unreal::kInvalidHook;
				if (Payday3::IsReady() && (m_bOutlinesApplied || m_bOutlinesActive))
					Unreal::RunOnGameThreadAndWait([this] { RestoreAllOutlined(); RestoreOutlines(); }, std::chrono::seconds(1));
			}

			void Render() override
			{
#ifdef _DEBUG
				if (DebugOn())
				{
					RenderDebug();
					return;
				}
#endif
				const stCategory& rEnemies = Category(ECategory::Enemies);
				const bool bPeople = rEnemies.AnyDrawn() || Category(ECategory::Civilians).AnyDrawn() || Category(ECategory::Players).AnyDrawn() || (VipOn() && m_stVip.AnyDrawn());
				const bool bCameras = CamerasShown();
				const bool bThings = ThingsShown();
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
				else if (const auto tpNow = std::chrono::steady_clock::now(); tpNow - m_tpBonesPruned > std::chrono::seconds(10))
				{
					m_tpBonesPruned = tpNow;
					std::erase_if(m_mapBones, [](const auto& rEntry) { return !Payday3::IsSameObject(rEntry.first, rEntry.second.iIndex); });
				}

				OmegaGUI::clDrawList& rList = OmegaGUI::GetBackgroundDrawList();
				g_stProjector.Prepare(pController);
				const SDK::FVector vecCamera = g_stProjector.bPrepared ? g_stProjector.Origin() : pController->PlayerCameraManager->GetCameraLocation();
				if (bPeople)
					DrawCharacters(rList, pController, vecCamera);
				if (bThings)
					DrawThings(rList, pController, pWorld, vecCamera, bCameras);
			}

			void Run() override
			{
				ScanThings();
				QueueOutlineColors();
				using Clock_t = std::chrono::steady_clock;
				if ((!WantsOutlines() && !m_bOutlinesActive) || !Payday3::IsReady() || Clock_t::now() < m_tpNextOutlineUpdate || m_bOutlineUpdateQueued.exchange(true))
					return;
				m_tpNextOutlineUpdate = Clock_t::now() + std::chrono::milliseconds(250);
				if (!Unreal::RunOnGameThread([this] {
						m_bOutlineUpdateQueued = false;
						Features::Guard(*this, "game thread", [this] {
							if (WantsOutlines())
								UpdateOutlined();
							else
								RestoreAllOutlined();
						});
					}))
					m_bOutlineUpdateQueued = false;
			}

			[[nodiscard]] std::string_view GetGroup() const override { return "visuals"; }

			[[nodiscard]] std::span<const stFeaturePage> GetPages() const override
			{
				static constexpr std::array<stFeaturePage, 2> s_aPages = { {
					{ "esp", "visuals.tab_esp" },
					{ "outlines", "visuals.group_outlines" },
				} };
				return s_aPages;
			}

			void DrawMenuPage(std::string_view strPage) override
			{
				if (strPage == "outlines")
				{
					DrawOutlinesTab();
					return;
				}
				if (strPage != "esp" || !OmegaGUI::BeginTabBar("##visuals.esp"))
					return;
				for (std::size_t uCategory = 0; uCategory < m_aCategories.size(); ++uCategory)
				{
					const stCategory& rCategory = m_aCategories[uCategory];
					if (!OmegaGUI::BeginTabItem(std::format("{}###visuals.esp.{}", OmegaLocalization::Get(rCategory.strTab), rCategory.strKey)))
						continue;
					DrawCategoryPage(uCategory);
					OmegaGUI::EndTabItem();
				}
#ifdef _DEBUG
				if (OmegaGUI::BeginTabItem("Debug###visuals.esp.debug"))
				{
					DrawDebugPage();
					OmegaGUI::EndTabItem();
				}
#endif
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
				std::int32_t iActorIndex = -1;
				std::vector<SDK::UMeshComponent*> aMeshes;
				SDK::USBZOutlineComponent* pOutline = nullptr;
			};

			[[nodiscard]] stCategory& Category(ECategory eCategory) { return m_aCategories[static_cast<std::size_t>(eCategory)]; }
			[[nodiscard]] const stCategory& Category(ECategory eCategory) const { return m_aCategories[static_cast<std::size_t>(eCategory)]; }

			[[nodiscard]] bool DebugOn() const
			{
#ifdef _DEBUG
				return *m_pDebugEnabled;
#else
				return false;
#endif
			}

			[[nodiscard]] bool VipOn() const
			{
				return stCategory::On(Category(ECategory::Objectives).pEnabled) && stCategory::On(m_pShowVips);
			}

			[[nodiscard]] bool AnyOutline() const
			{
				return (VipOn() && m_stVip.OutlineOn()) || std::ranges::any_of(m_aCategories, [](const stCategory& rCategory) { return rCategory.OutlineOn(); });
			}

			[[nodiscard]] bool HasAimbotTarget() const
			{
				return *m_pAimbotTarget && Payday3::GetAimbotTarget();
			}

			[[nodiscard]] bool WantsOutlines() const
			{
				return !DebugOn() && (AnyOutline() || HasAimbotTarget());
			}

			[[nodiscard]] static ECategory CategoryOf(Payday3::EItemType eType)
			{
				switch (eType)
				{
				case Payday3::EItemType::Cash:
				case Payday3::EItemType::MoneyMedium:
				case Payday3::EItemType::MoneyLarge:
				case Payday3::EItemType::Jewelry:
				case Payday3::EItemType::RareMinerals:
				case Payday3::EItemType::RareStone:
				case Payday3::EItemType::Cocaine:
				case Payday3::EItemType::SatellitePart:
				case Payday3::EItemType::CigarBox:
				case Payday3::EItemType::Container:
				case Payday3::EItemType::ATM:
				case Payday3::EItemType::Plank:
					return ECategory::Loot;
				case Payday3::EItemType::MedkitSmall:
				case Payday3::EItemType::MedkitLarge:
				case Payday3::EItemType::Armor:
				case Payday3::EItemType::Ammo:
					return ECategory::Supplies;
				case Payday3::EItemType::KeycardReader:
				case Payday3::EItemType::Lab:
				case Payday3::EItemType::HydrogenChloride:
				case Payday3::EItemType::MuriaticAcid:
				case Payday3::EItemType::CausticSoda:
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

			void DrawCategoryPage(std::size_t uCategory)
			{
				using namespace OmegaLocalization::Literals;
				const stCategory& rCategory = m_aCategories[uCategory];
				OmegaGUI::Checkbox(std::format("{}###visuals.{}.enabled", "visuals.enabled"_t, rCategory.strKey), rCategory.pEnabled);
				OmegaGUI::ItemTooltip("visuals.enabled_hint"_t);
				if (!*rCategory.pEnabled)
					return;
				OmegaGUI::Spacing();
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
				if (eCategory == ECategory::Enemies && (stCategory::On(rCategory.pBox) || stCategory::On(rCategory.pName) || stCategory::On(rCategory.pDistance) || stCategory::On(rCategory.pOutline)))
				{
					OmegaGUI::Separator();
					OmegaGUI::Checkbox("visuals.cameras"_l, m_pCameras);
					OmegaGUI::ItemTooltip("visuals.cameras_hint"_t);
				}
				if (eCategory == ECategory::Enemies)
				{
					OmegaGUI::Separator();
					OmegaGUI::Checkbox("visuals.aimbot_target"_l, m_pAimbotTarget);
					OmegaGUI::ItemTooltip("visuals.aimbot_target_hint"_t);
				}
				if (eCategory == ECategory::Players)
				{
					OmegaGUI::Separator();
					OmegaGUI::Checkbox("visuals.omegaware_tag"_l, m_pOmegaTag);
					OmegaGUI::ItemTooltip("visuals.omegaware_tag_hint"_t);
				}
				if (rCategory.bPeople)
					return;
				OmegaGUI::Separator();
				for (const stItemSetting& rSetting : m_aItems)
					if (CategoryOf(rSetting.eType) == eCategory)
						OmegaGUI::Checkbox(std::format("{}###{}", OmegaLocalization::Get(rSetting.strShow), rSetting.strShow), rSetting.pShow);
				if (eCategory == ECategory::Objectives)
				{
					OmegaGUI::Checkbox("visuals.show_drop_offs"_l, m_pShowDropOffs);
					OmegaGUI::Separator();
					OmegaGUI::Checkbox("visuals.show_vips"_l, m_pShowVips);
					OmegaGUI::ItemTooltip("visuals.show_vips_hint"_t);
					if (*m_pShowVips)
					{
						auto fnVipOption = [&](std::string_view strText, std::string_view strOption, bool* pValue) {
							OmegaGUI::Checkbox(std::format("{}###visuals.vip.{}", OmegaLocalization::Get(strText), strOption), pValue);
						};
						fnVipOption("visuals.box", "box", m_stVip.pBox);
						fnVipOption("visuals.name", "name", m_stVip.pName);
						fnVipOption("visuals.distance", "distance", m_stVip.pDistance);
						fnVipOption("visuals.outline", "outline", m_stVip.pOutline);
					}
				}
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
				if (eCategory == ECategory::Enemies || eCategory == ECategory::Civilians || eCategory == ECategory::Players)
				{
					const bool bCop = eCategory == ECategory::Enemies;
					const bool bPlayer = eCategory == ECategory::Players;
					auto fnPair = [&](std::string_view strText, std::string_view strCop, std::string_view strCivilian, std::string_view strPlayer, const stColorPair& stColors) {
						fnRow(strText, bPlayer ? strPlayer : (bCop ? strCop : strCivilian), bPlayer ? stColors.pPlayer : (bCop ? stColors.pCop : stColors.pCivilian));
					};
					fnPair("visuals.box", "visuals.box_cop_color", "visuals.box_civilian_color", "visuals.box_player_color", m_stBoxColors);
					fnPair("visuals.name", "visuals.name_cop_color", "visuals.name_civilian_color", "visuals.name_player_color", m_stNameColors);
					fnPair("visuals.distance", "visuals.distance_cop_color", "visuals.distance_civilian_color", "visuals.distance_player_color", m_stDistanceColors);
					fnPair("visuals.health_bar", "visuals.health_bar_cop_color", "visuals.health_bar_civilian_color", "visuals.health_bar_player_color", m_stHealthColors);
					if (bCop || bPlayer)
						fnRow("visuals.armor_bar", "visuals.armor_bar_color", m_pArmorColor);
					if (bCop)
						fnRow("visuals.aimbot_target", "visuals.aimbot_target_color", m_pAimbotTargetColor);
					fnPair("visuals.skeleton", "visuals.skeleton_cop_color", "visuals.skeleton_civilian_color", "visuals.skeleton_player_color", m_stSkeletonColors);
				}
				else
				{
					for (const stItemSetting& rSetting : m_aItems)
						if (CategoryOf(rSetting.eType) == eCategory)
							fnRow(rSetting.strShow, rSetting.strColor, rSetting.pColor);
					if (eCategory == ECategory::Objectives)
					{
						fnRow("visuals.show_drop_offs", "visuals.drop_off_color", m_pDropOffColor);
						fnRow("visuals.show_vips", "visuals.vip_color", m_pVipColor);
					}
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
					if (*m_pOutlineColors && OmegaGUI::BeginTable("##visuals.outline_colors", 2, OmegaGUI::ETableFlags::RowBackground))
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
					if (*m_pOutlineColors && OmegaGUI::SmallButton("visuals.outline_reset"_l))
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
						Features::Guard(*this, "game thread", [this] {
							if (*m_pOutlineColors)
								ApplyOutlines();
							else
								RestoreOutlines();
						});
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
				if (const auto itOutlined = m_mapOutlined.find(pActor); itOutlined != m_mapOutlined.end() && itOutlined->second.iActorIndex == pActor->Index)
					return itOutlined->second;
				stOutlined stEntry;
				stEntry.iActorIndex = pActor->Index;
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
				return m_mapOutlined.insert_or_assign(pActor, std::move(stEntry)).first->second;
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
					if (Payday3::IsAliveActor(pActor) && pActor->Index == rEntry.iActorIndex)
						RestoreOutlined(rEntry);
				m_mapOutlined.clear();
				m_bOutlinesActive = false;
			}

			void UpdateOutlined()
			{
				const Profiler::clScope clProfile("visuals.update outlines");
				std::erase_if(m_mapOutlined, [](const auto& rEntry) { return !Payday3::IsAliveActor(rEntry.first) || rEntry.first->Index != rEntry.second.iActorIndex; });

				std::unordered_map<SDK::AActor*, std::pair<int, SDK::USBZOutlineComponent*>> mapWanted;
				const std::shared_ptr<const Payday3::stWorldSnapshot> pWorldSnapshot = Payday3::GetWorldSnapshot();
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				if (pWorldSnapshot && pController && pWorldSnapshot->pWorld == Unreal::GetWorld())
				{
					SDK::APawn* pLocalPawn = pController->AcknowledgedPawn;
					for (const Payday3::stPawnEntry& rPawn : pWorldSnapshot->aPawns)
					{
						SDK::AActor* pActor = rPawn.pActor;
						if (!Payday3::IsAliveActor(pActor) || pActor == pLocalPawn || pActor->Class != rPawn.pClass || !rPawn.bAlive)
							continue;
						const bool bPlayerPawn = rPawn.bPlayer;
						const Payday3::EEnemyCategory eKind = rPawn.pInfo->eCategory;
						if (!bPlayerPawn && eKind != Payday3::EEnemyCategory::Cop && eKind != Payday3::EEnemyCategory::Civilian)
							continue;
						const ECategory eCategory = bPlayerPawn ? ECategory::Players : (eKind == Payday3::EEnemyCategory::Cop ? ECategory::Enemies : ECategory::Civilians);
						const bool bVip = eCategory == ECategory::Civilians && VipOn() && rPawn.bVip;
						const bool bAimbotTarget = *m_pAimbotTarget && pActor == Payday3::GetAimbotTarget();
						if ((bVip ? m_stVip : Category(eCategory)).OutlineOn() || bAimbotTarget)
						{
							int iColorIndex = kPeopleOutlineIndex;
							SDK::USBZOutlineComponent* pPawnOutline = Payday3::GetPawnOutline(pActor);
							if (!bAimbotTarget && bPlayerPawn && Payday3::IsAlive(pPawnOutline) && Payday3::IsAlive(pPawnOutline->ActiveAsset))
								iColorIndex = pPawnOutline->ActiveAsset->ColorIndex;
							mapWanted[pActor] = { iColorIndex, pPawnOutline };
						}
					}
				}

				const bool bCameras = *m_pCameras && Category(ECategory::Enemies).OutlineOn();
				const bool bItems = Category(ECategory::Loot).OutlineOn() || Category(ECategory::KeyItems).OutlineOn() || Category(ECategory::Objectives).OutlineOn() || Category(ECategory::Supplies).OutlineOn();
				if (bCameras || bItems)
				{
					std::shared_ptr<const stThingSnapshot> pSnapshot;
					{
						std::scoped_lock lckThings(m_mtxThings);
						pSnapshot = m_pThings;
					}
					if (pSnapshot && pSnapshot->pWorld == Unreal::GetWorld())
						for (const stThing& rThing : pSnapshot->aThings)
						{
							SDK::AActor* pActor = rThing.pActor;
							if (!Payday3::IsAliveActor(pActor) || pActor->Class != rThing.pClass)
								continue;
							if (rThing.eKind == EThingKind::Camera)
							{
								auto* pCamera = static_cast<SDK::ASBZSecurityCamera*>(pActor);
								if (bCameras && pCamera->CameraState != SDK::ESBZCameraState::Destroyed)
									mapWanted[pActor] = { kPeopleOutlineIndex, pCamera->OutlineComponent };
								continue;
							}
							if (rThing.eKind != EThingKind::Item || !bItems)
								continue;
							const Payday3::stItemInfo& rItem = rThing.stItem;
							const stItemSetting* pSetting = Setting(rItem.eType);
							if (!pSetting || !*pSetting->pShow || !Category(CategoryOf(rItem.eType)).OutlineOn())
								continue;
							if (IsDepleted(rItem.eType, pActor) || HoldsKeyFor(rItem))
								continue;
							mapWanted[pActor] = { kItemOutlineIndex, nullptr };
						}
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
				{
					stOutlined& rEntry = Outlined(pActor, prWanted.second);
					if (!std::ranges::all_of(rEntry.aMeshes, [](SDK::UMeshComponent* pMesh) { return Payday3::IsAlive(pMesh); }))
					{
						m_mapOutlined.erase(pActor);
						continue;
					}
					OmegaRenderHook::stFault stFault;
					auto fnApply = [&] { SetMeshStencil(rEntry.aMeshes, true, prWanted.first); };
					if (!OmegaRenderHook::InvokeGuarded(fnApply, stFault))
					{
						LogWarning("outlines: dropped a stale outline entry (code 0x{:08X})", stFault.uCode);
						m_mapOutlined.erase(pActor);
					}
				}
				m_bOutlinesActive = !m_mapOutlined.empty();
			}

			void DrawCharacters(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, const SDK::FVector& vecCamera)
			{
				const Profiler::clScope clProfile("visuals.draw characters");
				const std::shared_ptr<const Payday3::stWorldSnapshot> pSnapshot = Payday3::GetWorldSnapshot();
				if (!pSnapshot || pSnapshot->pWorld != Unreal::GetWorld())
					return;

				SDK::APawn* pLocalPawn = pController->AcknowledgedPawn;
				for (const Payday3::stPawnEntry& rPawn : pSnapshot->aPawns)
				{
					SDK::AActor* pActor = rPawn.pActor;
					if (pActor == pLocalPawn || !rPawn.bAlive || !Memory::IsValidObjectPtr(pActor) || pActor->Class != rPawn.pClass || !Memory::IsValidObjectPtr(rPawn.pMesh))
						continue;
					SDK::USkeletalMeshComponent* pMesh = rPawn.pMesh;
					const bool bDrone = rPawn.bDrone;
					const bool bPlayerPawn = rPawn.bPlayer;
					const Payday3::stEnemyInfo& stInfo = *rPawn.pInfo;
					if (!bPlayerPawn && stInfo.eCategory != Payday3::EEnemyCategory::Cop && stInfo.eCategory != Payday3::EEnemyCategory::Civilian)
						continue;
					const bool bCop = stInfo.eCategory == Payday3::EEnemyCategory::Cop;
					const bool bVip = rPawn.bVip && VipOn();
					const EColorSet eSet = bPlayerPawn ? EColorSet::Player : (bVip ? EColorSet::Vip : (bCop ? EColorSet::Cop : EColorSet::Civilian));
					const stCategory& rCategory = bVip ? m_stVip : Category(bPlayerPawn ? ECategory::Players : (bCop ? ECategory::Enemies : ECategory::Civilians));
					const std::string& strPlayerName = rPawn.strName;
					const Payday3::EModUserKind eModKind = rPawn.eModKind;
					if (!rCategory.AnyDrawn())
						continue;
					const bool bTarget = *m_pAimbotTarget && pActor == Payday3::GetAimbotTarget();

					if (stCategory::On(rCategory.pBox) || bTarget || *rCategory.pName || *rCategory.pDistance || stCategory::On(rCategory.pHealthBar) || stCategory::On(rCategory.pArmorBar))
					{
						const SDK::FVector vecActor = Payday3::GetActorLocation(pActor);
						const std::optional<stBox> optBox = bDrone ? CalculateActorBox(pController, pActor) : CalculateBox(pController, pActor, pMesh, vecCamera, vecActor);
						if (!optBox)
							continue;
						const bool bStats = stCategory::On(rCategory.pHealthBar) || stCategory::On(rCategory.pArmorBar);
						const stPawnStats stStats = bStats ? ReadPawnStats(pActor).value_or(stPawnStats{}) : stPawnStats{};
						const float flDistance = static_cast<float>((vecActor - vecCamera).Magnitude() / 100.0);
						DrawBox(rList, *optBox, bPlayerPawn ? std::string_view(strPlayerName) : (bVip ? std::string_view(EspText("VIP")) : std::string_view(EspText(stInfo.strName))), flDistance, stStats, rCategory, eSet, *m_pOmegaTag ? eModKind : Payday3::EModUserKind::None, bTarget);
					}

					if (!bDrone && stCategory::On(rCategory.pSkeleton))
						DrawSkeleton(rList, pController, pMesh, bTarget ? *m_pAimbotTargetColor : m_stSkeletonColors.Get(eSet));
				}
			}

			void DrawBox(OmegaGUI::clDrawList& rList, const stBox& stRect, std::string_view strName, float flDistance, const stPawnStats& stStats, const stCategory& rCategory, EColorSet eSet, Payday3::EModUserKind eModKind, bool bTarget) const
			{
				const float flBoxWidth = stRect.flRight - stRect.flLeft;
				const float flBoxHeight = stRect.flBottom - stRect.flTop;
				if (!std::isfinite(flBoxWidth) || !std::isfinite(flBoxHeight) || flBoxWidth <= 0.f || flBoxHeight <= 0.f)
					return;

				if (bTarget)
				{
					const OmegaGUI::color colTarget = *m_pAimbotTargetColor;
					rList.AddRectFilled({ stRect.flLeft, stRect.flTop }, { stRect.flRight, stRect.flBottom }, colTarget.WithAlpha(40));
					rList.AddRect({ stRect.flLeft - 2.f, stRect.flTop - 2.f }, { stRect.flRight + 2.f, stRect.flBottom + 2.f }, kOutline, 0.f, 3.f);
					rList.AddRect({ stRect.flLeft, stRect.flTop }, { stRect.flRight, stRect.flBottom }, colTarget, 0.f, 3.f);
				}
				else if (stCategory::On(rCategory.pBox))
				{
					rList.AddRect({ stRect.flLeft - 1.f, stRect.flTop - 1.f }, { stRect.flRight + 1.f, stRect.flBottom + 1.f }, kOutline, 0.f, 1.6f);
					rList.AddRect({ stRect.flLeft, stRect.flTop }, { stRect.flRight, stRect.flBottom }, m_stBoxColors.Get(eSet), 0.f, 1.5f);
				}

				if (eModKind != Payday3::EModUserKind::None)
				{
					const std::string_view kTag = eModKind == Payday3::EModUserKind::Developer ? OmegaLocalization::Get("modusers.tag_dev") : OmegaLocalization::Get("modusers.tag");
					const OmegaGUI::vec2 vecTagSize = OmegaGUI::CalcTextSize(kTag);
					const float flNameLift = (*rCategory.pName || *rCategory.pDistance) ? vecTagSize.y : 0.f;
					DrawOutlinedText(rList, { stRect.flLeft + flBoxWidth * .5f - vecTagSize.x * .5f, stRect.flTop - 4.f - flNameLift - vecTagSize.y }, OmegaGUI::color(0, 255, 255), kTag);
				}
				if (*rCategory.pName || *rCategory.pDistance)
					DrawNameAndDistance(rList, stRect.flLeft + flBoxWidth * .5f, stRect.flTop - 4.f, *rCategory.pName ? FormatEntityName(strName) : std::string(), bTarget ? *m_pAimbotTargetColor : m_stNameColors.Get(eSet), *rCategory.pDistance ? FormatDistance(flDistance) : std::string(), bTarget ? *m_pAimbotTargetColor : m_stDistanceColors.Get(eSet));

				constexpr float flBarWidth = 4.f;
				if (stCategory::On(rCategory.pHealthBar))
				{
					const float flFraction = stStats.flHealthMax > 0.f ? std::clamp(stStats.flHealth / stStats.flHealthMax, 0.f, 1.f) : 0.f;
					rList.AddRectFilled({ stRect.flLeft - flBarWidth - 3.f, stRect.flTop }, { stRect.flLeft - 1.f, stRect.flBottom }, kOutline);
					rList.AddRectFilled({ stRect.flLeft - flBarWidth - 2.f, stRect.flBottom - flBoxHeight * flFraction }, { stRect.flLeft - 2.f, stRect.flBottom }, m_stHealthColors.Get(eSet));
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
				if (!bInserted && itCache->second.iIndex == pMesh->Index)
					return itCache->second;

				itCache->second.iIndex = pMesh->Index;
				itCache->second.aIndices.fill(-1);
				const int iBones = pMesh->GetNumBones();
				for (int iBone = 0; iBone < iBones; ++iBone)
				{
					const SDK::FName nameBone = pMesh->GetBoneName(iBone);
					const std::string strBone = nameBone.ToString();
					if (auto itName = std::ranges::find(g_aBoneNames, strBone); itName != g_aBoneNames.end())
					{
						const auto uBone = static_cast<std::size_t>(itName - g_aBoneNames.begin());
						itCache->second.aIndices[uBone] = iBone;
						itCache->second.aNames[uBone] = nameBone;
					}
				}
				return itCache->second;
			}

			void DrawSkeleton(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::USkeletalMeshComponent* pMesh, OmegaGUI::color col)
			{
				const BoneCache_t& rBones = Bones(pMesh);
				std::array<std::optional<OmegaGUI::vec2>, BoneCount> aScreen;
				std::array<bool, BoneCount> aProjected{};
				auto fnScreen = [&](EBone eBone) -> const std::optional<OmegaGUI::vec2>& {
					if (!aProjected[eBone])
					{
						aProjected[eBone] = true;
						if (rBones.aIndices[eBone] >= 0)
							aScreen[eBone] = Project(pController, pMesh->GetSocketLocation(rBones.aNames[eBone]));
					}
					return aScreen[eBone];
				};
				for (const auto& [eParent, eChild] : g_aSkeleton)
				{
					const std::optional<OmegaGUI::vec2>& optParent = fnScreen(eParent);
					const std::optional<OmegaGUI::vec2>& optChild = fnScreen(eChild);
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

			[[nodiscard]] static bool IsCollectedIngredient(SDK::AActor* pActor)
			{
				if (!pActor->IsA(SDK::ABP_MethIngredientBase_C::StaticClass()))
					return false;
				auto* pIngredient = static_cast<SDK::ABP_MethIngredientBase_C*>(pActor);
				if (pActor->bHidden)
					return true;
				return Memory::IsValidObjectPtr(pIngredient->StaticMesh) && !pIngredient->StaticMesh->bVisible;
			}

			[[nodiscard]] static bool IsLootedAtm(SDK::AActor* pActor)
			{
				if (!pActor->IsA(SDK::ASBZBreakableMachine::StaticClass()))
					return false;
				auto* pMachine = static_cast<SDK::ASBZBreakableMachine*>(pActor);
				const int iCount = OW_FIELD(pMachine, InstantLootCount);
				if (iCount > 0)
					return OW_FIELD(pMachine, InstantLootTaken) >= iCount;
				return OW_FIELD(pMachine, CurrentState) == SDK::ESBZBreakableMachineState::OpenBroken;
			}

			[[nodiscard]] static bool IsLootedInstant(SDK::AActor* pActor)
			{
				if (!pActor->IsA(SDK::ASBZInstantLoot::StaticClass()))
					return false;
				return pActor->bHidden || static_cast<SDK::ASBZInstantLoot*>(pActor)->bIsLooted;
			}

			[[nodiscard]] static std::uint8_t CrateFlags(SDK::UClass* pClass)
			{
				thread_local std::unordered_map<SDK::UClass*, std::uint8_t> t_mapCrates;
				if (t_mapCrates.size() > 512)
					t_mapCrates.clear();
				const auto [itClass, bInserted] = t_mapCrates.try_emplace(pClass, std::uint8_t{ 0 });
				if (bInserted)
					for (const SDK::UStruct* pStruct = pClass; pStruct; pStruct = pStruct->SuperStruct)
					{
						const std::string strName = pStruct->Name.ToString();
						if (strName == "BP_WoodenCrate_C")
							itClass->second |= 1;
						else if (strName == "BP_WoodenCrate_SatellitePart_LowValue_C")
							itClass->second |= 2;
					}
				return itClass->second;
			}

			[[nodiscard]] bool HoldsKeyFor(const Payday3::stItemInfo& rItem) const
			{
				std::string_view strColor;
				if (rItem.strLabel.find("Blue") != std::string_view::npos)
					strColor = "blue";
				else if (rItem.strLabel.find("Red") != std::string_view::npos)
					strColor = "red";
				std::string_view strKind;
				switch (rItem.eType)
				{
				case Payday3::EItemType::Keycard:
					strKind = "card";
					break;
				case Payday3::EItemType::RFIDTag:
					strKind = "rfid";
					break;
				case Payday3::EItemType::USB:
					strKind = "usb";
					break;
				case Payday3::EItemType::QRPhone:
					strKind = "phone";
					break;
				default:
					return false;
				}
				const bool bColored = rItem.eType == Payday3::EItemType::Keycard || rItem.eType == Payday3::EItemType::RFIDTag;
				if (bColored && strColor.empty())
					return false;
				std::scoped_lock lckHeld(m_mtxHeldKeys);
				for (const auto& [strKey, iCount] : m_mapHeldKeys)
					if (iCount > 0 && strKey.find(strKind) != std::string::npos && (!bColored || strKey.find(strColor) != std::string::npos))
						return true;
				return false;
			}

			[[nodiscard]] static bool IsUsableInteractable(SDK::USBZInteractableComponent* pInteractable)
			{
				return Payday3::IsAlive(pInteractable) && pInteractable->bInteractionEnabled && !pInteractable->bIsHidden;
			}

			[[nodiscard]] static bool IsUsbDriveClass(SDK::UClass* pClass)
			{
				thread_local std::unordered_map<SDK::UClass*, bool> t_mapUsbClasses;
				if (t_mapUsbClasses.size() > 512)
					t_mapUsbClasses.clear();
				const auto [itClass, bInserted] = t_mapUsbClasses.try_emplace(pClass, false);
				if (bInserted)
					for (SDK::UStruct* pStruct = pClass; pStruct && !itClass->second; pStruct = static_cast<SDK::UStruct*>(pStruct->SuperStruct))
						itClass->second = pStruct->Name.ToString() == "BP_USBDrive_C";
				return itClass->second;
			}

			[[nodiscard]] static bool IsUnusableUsb(SDK::AActor* pActor)
			{
				if (!pActor->Class || !IsUsbDriveClass(pActor->Class))
					return true;
				auto* pUsb = static_cast<SDK::ABP_USBDrive_C*>(pActor);
				return !IsUsableInteractable(pUsb->SBZInteractable) && !IsUsableInteractable(pUsb->SBZInteractableGhost);
			}

			[[nodiscard]] static bool IsDepleted(Payday3::EItemType eType, SDK::AActor* pActor)
			{
				switch (eType)
				{
				case Payday3::EItemType::USB:
					return IsUnusableUsb(pActor);
				case Payday3::EItemType::RFIDTag:
					return pActor->bHidden;
				case Payday3::EItemType::RareMinerals:
				case Payday3::EItemType::RareStone:
				case Payday3::EItemType::SatellitePart:
				case Payday3::EItemType::CigarBox:
					return pActor->bHidden || !IsUsableInteractable(static_cast<SDK::ASBZSingleBagGenerator*>(pActor)->Interactable);
				case Payday3::EItemType::Container:
					if (const std::uint8_t uCrate = pActor->Class ? CrateFlags(pActor->Class) : std::uint8_t{ 0 }; (uCrate & 1) != 0)
						return pActor->bHidden || !IsUsableInteractable(static_cast<SDK::ABP_WoodenCrate_C*>(pActor)->SBZInteractable);
					return pActor->bHidden || !IsUsableInteractable(static_cast<SDK::ASBZItemContainer*>(pActor)->InteractableComponent);
				case Payday3::EItemType::Cocaine:
					if (pActor->IsA(SDK::ASBZInstantLoot::StaticClass()))
						return IsLootedInstant(pActor);
					if (pActor->IsA(SDK::ASBZSingleBagGenerator::StaticClass()))
						return pActor->bHidden || !IsUsableInteractable(static_cast<SDK::ASBZSingleBagGenerator*>(pActor)->Interactable);
					if (pActor->IsA(SDK::ASBZMultiBagGenerator::StaticClass()))
						return pActor->bHidden || !IsUsableInteractable(static_cast<SDK::ASBZMultiBagGenerator*>(pActor)->InteractableComponent);
					return false;
				case Payday3::EItemType::Cash:
				case Payday3::EItemType::MoneyMedium:
				case Payday3::EItemType::Jewelry:
					return IsLootedInstant(pActor);
				case Payday3::EItemType::DepositBox:
					return pActor->IsA(SDK::ASBZMiniGameActor::StaticClass()) && static_cast<SDK::ASBZMiniGameActor*>(pActor)->bIsUnlocked;
				case Payday3::EItemType::KeycardReader:
					return pActor->IsA(SDK::ASBZKeycardReader::StaticClass()) && !IsUsableInteractable(static_cast<SDK::ASBZKeycardReader*>(pActor)->InteractableComponent);
				case Payday3::EItemType::QRPhone:
					return IsCollectedPhone(pActor);
				case Payday3::EItemType::ATM:
					return IsLootedAtm(pActor);
				case Payday3::EItemType::HydrogenChloride:
				case Payday3::EItemType::MuriaticAcid:
				case Payday3::EItemType::CausticSoda:
					return IsCollectedIngredient(pActor);
				default:
					return false;
				}
			}

			[[nodiscard]] static float DistanceMeters(const SDK::FVector& vecActor, const SDK::FVector& vecCamera)
			{
				return static_cast<float>((vecActor - vecCamera).Magnitude() / 100.0);
			}

			[[nodiscard]] static float DistanceMeters(SDK::AActor* pActor, const SDK::FVector& vecCamera)
			{
				return DistanceMeters(Payday3::GetActorLocation(pActor), vecCamera);
			}

			void DrawCamera(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::ASBZSecurityCamera* pCamera, const SDK::FVector& vecCamera) const
			{
				if (pCamera->CameraState == SDK::ESBZCameraState::Destroyed || !Payday3::IsAliveActor(pCamera))
					return;
				const SDK::FVector vecActor = Payday3::GetActorLocation(pCamera);
				const std::optional<OmegaGUI::vec2> optScreen = Project(pController, vecActor);
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
					const std::string_view strLabel = EspText(byAlpha == 255 ? "Camera" : "Camera (off)");
					DrawNameAndDistance(rList, optScreen->x, optScreen->y - 8.f, *rEnemies.pName ? strLabel : std::string_view(), m_stNameColors.pCop->WithAlpha(byAlpha), *rEnemies.pDistance ? FormatDistance(DistanceMeters(vecActor, vecCamera)) : std::string(), m_stDistanceColors.pCop->WithAlpha(byAlpha));
				}
			}

			void DrawDropOff(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::ASBZBagTriggerVolume* pVolume, const SDK::FVector& vecCamera) const
			{
				const SDK::FVector vecActor = Payday3::GetActorLocation(pVolume);
				const std::optional<OmegaGUI::vec2> optScreen = Project(pController, vecActor);
				if (!optScreen)
					return;
				const stCategory& rObjectives = Category(ECategory::Objectives);
				const bool bActive = pVolume->GetActorEnableCollision();
				const OmegaGUI::color col = bActive ? *m_pDropOffColor : m_pDropOffColor->WithAlpha(110);
				rList.AddCircle(*optScreen, 6.f, kOutline, 16, 3.f);
				rList.AddCircle(*optScreen, 6.f, col, 16, 1.5f);
				const std::string_view strLabel = EspText(bActive ? "Drop-off" : "Drop-off (inactive)");
				DrawNameAndDistance(rList, optScreen->x, optScreen->y - 10.f, *rObjectives.pName ? strLabel : std::string_view(), col, *rObjectives.pDistance ? FormatDistance(DistanceMeters(vecActor, vecCamera)) : std::string(), col);
			}

#ifdef _DEBUG
			static constexpr std::size_t kDebugActorSlice = 600;
			static constexpr std::size_t kDebugBoneTargets = 8;
			static constexpr std::chrono::milliseconds kDebugCycleMinimum{ 250 };

			struct stDebugBones
			{
				std::int32_t iIndex = -1;
				std::vector<SDK::FName> aNames;
				std::vector<int> aParents;
				std::vector<std::string> aNameText;
				std::vector<std::string> aIndexText;
				std::vector<std::string> aBothText;
			};

			struct stDebugBoneTarget
			{
				SDK::USkeletalMeshComponent* pMesh = nullptr;
				float flDistance = 0.f;
			};

			void DrawDebugPage()
			{
				OmegaGUI::Checkbox("Debug ESP###visuals.debug.enabled", m_pDebugEnabled);
				OmegaGUI::ItemTooltip("replaces every other esp and outline while on");
				if (!*m_pDebugEnabled)
					return;
				OmegaGUI::Spacing();
				OmegaGUI::BeginColumns(2);
				if (OmegaGUI::BeginSection("Actors###visuals.debug.section_actors"))
				{
					OmegaGUI::Checkbox("class names###visuals.debug.actors", m_pDebugActors);
					OmegaGUI::ItemTooltip("labels every actor in range with its class");
					if (*m_pDebugActors)
					{
						OmegaGUI::Checkbox("full hierarchy###visuals.debug.hierarchy", m_pDebugHierarchy);
						OmegaGUI::ItemTooltip("also lists every parent class below the actor class");
						OmegaGUI::SliderFloat("range###visuals.debug.actor_distance", m_pDebugActorDistance, 1.f, 200.f, "{:.0f} m");
					}
					OmegaGUI::EndSection();
				}
				OmegaGUI::NextColumn();
				if (OmegaGUI::BeginSection("Bones###visuals.debug.section_bones"))
				{
					OmegaGUI::Checkbox("bones###visuals.debug.bones", m_pDebugBones);
					OmegaGUI::ItemTooltip("debug skeleton of every pawn in range, built from the mesh's own bone hierarchy");
					if (*m_pDebugBones)
					{
						OmegaGUI::Checkbox("bone lines###visuals.debug.bone_lines", m_pDebugBoneLines);
						OmegaGUI::Checkbox("bone indices###visuals.debug.bone_indices", m_pDebugBoneIndices);
						OmegaGUI::Checkbox("bone names###visuals.debug.bone_names", m_pDebugBoneNames);
						OmegaGUI::SliderFloat("range###visuals.debug.bone_distance", m_pDebugBoneDistance, 1.f, 200.f, "{:.0f} m");
					}
					OmegaGUI::EndSection();
				}
				OmegaGUI::EndColumns();
			}

			void RenderDebug()
			{
				if (!Payday3::IsReady())
					return;
				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				if (!pWorld || !Memory::IsValidObjectPtr(pWorld->PersistentLevel) || !pController || !Memory::IsValidObjectPtr(pController->PlayerCameraManager))
					return;

				if (pWorld != m_pDebugWorld)
				{
					m_pDebugWorld = pWorld;
					m_aDebugActors.clear();
					m_aDebugNear.clear();
					m_aDebugNearNext.clear();
					m_uDebugCursor = 0;
					m_mapDebugClasses.clear();
					m_mapDebugBones.clear();
				}

				OmegaGUI::clDrawList& rList = OmegaGUI::GetBackgroundDrawList();
				g_stProjector.Prepare(pController);
				const SDK::FVector vecCamera = g_stProjector.bPrepared ? g_stProjector.Origin() : pController->PlayerCameraManager->GetCameraLocation();
				if (*m_pDebugActors)
				{
					RefreshDebugActors(pWorld, vecCamera);
					DrawDebugActors(rList, pController, vecCamera);
				}
				if (*m_pDebugBones)
					DrawDebugBones(rList, pController, vecCamera);
			}

			void RefreshDebugActors(SDK::UWorld* pWorld, const SDK::FVector& vecCamera)
			{
				using Clock_t = std::chrono::steady_clock;
				if (m_uDebugCursor >= m_aDebugActors.size())
				{
					if (Clock_t::now() < m_tpDebugCycle)
						return;
					if (!m_aDebugActors.empty())
						m_aDebugNear.swap(m_aDebugNearNext);
					m_aDebugNearNext.clear();
					m_aDebugActors.clear();
					Payday3::ForEachLevelActor(pWorld, [&](SDK::AActor* pActor) { m_aDebugActors.push_back(pActor); });
					m_uDebugCursor = 0;
					m_tpDebugCycle = Clock_t::now() + kDebugCycleMinimum;
				}

				const float flReach = *m_pDebugActorDistance * 1.5f + 5.f;
				const std::size_t uEnd = std::min(m_uDebugCursor + kDebugActorSlice, m_aDebugActors.size());
				for (; m_uDebugCursor < uEnd; ++m_uDebugCursor)
				{
					SDK::AActor* pActor = m_aDebugActors[m_uDebugCursor];
					if (Payday3::IsAliveActor(pActor) && DistanceMeters(pActor, vecCamera) <= flReach)
						m_aDebugNearNext.push_back(pActor);
				}
			}

			const std::vector<std::string>& DebugClassChain(SDK::UClass* pClass)
			{
				auto [itChain, bInserted] = m_mapDebugClasses.try_emplace(pClass);
				if (bInserted)
					for (SDK::UStruct* pStruct = pClass; pStruct; pStruct = static_cast<SDK::UStruct*>(pStruct->SuperStruct))
						itChain->second.push_back(pStruct->Name.ToString());
				return itChain->second;
			}

			void DrawDebugActors(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, const SDK::FVector& vecCamera)
			{
				constexpr OmegaGUI::color colText(255, 0, 0);
				for (SDK::AActor* pActor : m_aDebugNear)
				{
					if (!Payday3::IsAliveActor(pActor) || !pActor->Class || DistanceMeters(pActor, vecCamera) > *m_pDebugActorDistance)
						continue;
					const std::optional<OmegaGUI::vec2> optScreen = Project(pController, pActor->K2_GetActorLocation());
					if (!optScreen)
						continue;
					const std::vector<std::string>& rChain = DebugClassChain(pActor->Class);
					const std::size_t uLines = *m_pDebugHierarchy ? rChain.size() : std::min<std::size_t>(rChain.size(), 1);
					float flY = optScreen->y - 8.f;
					for (std::size_t uLine = 0; uLine < uLines; ++uLine)
					{
						const OmegaGUI::vec2 vecSize = OmegaGUI::CalcTextSize(rChain[uLine]);
						DrawOutlinedText(rList, { optScreen->x - vecSize.x * .5f, flY }, colText, rChain[uLine]);
						flY += vecSize.y + 2.f;
					}
				}
			}

			const stDebugBones& DebugBones(SDK::USkeletalMeshComponent* pMesh)
			{
				auto [itCache, bInserted] = m_mapDebugBones.try_emplace(pMesh);
				stDebugBones& rBones = itCache->second;
				const int iBones = pMesh->GetNumBones();
				if (!bInserted && rBones.iIndex == pMesh->Index && static_cast<int>(rBones.aNames.size()) == iBones)
					return rBones;

				rBones = {};
				rBones.iIndex = pMesh->Index;
				rBones.aNames.reserve(iBones);
				rBones.aParents.reserve(iBones);
				for (int iBone = 0; iBone < iBones; ++iBone)
				{
					const SDK::FName nameBone = pMesh->GetBoneName(iBone);
					rBones.aNames.push_back(nameBone);
					rBones.aParents.push_back(pMesh->GetBoneIndex(pMesh->GetParentBone(nameBone)));
					std::string strName = nameBone.ToString();
					std::string strIndex = std::to_string(iBone);
					rBones.aBothText.push_back(std::format("{} {}", strIndex, strName));
					rBones.aNameText.push_back(std::move(strName));
					rBones.aIndexText.push_back(std::move(strIndex));
				}
				return rBones;
			}

			void DrawDebugBones(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, const SDK::FVector& vecCamera)
			{
				SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns();
				if (!pPawns)
					return;

				std::erase_if(m_mapDebugBones, [](const auto& rEntry) { return !Payday3::IsSameObject(rEntry.first, rEntry.second.iIndex); });

				std::vector<stDebugBoneTarget> aTargets;
				SDK::APawn* pLocalPawn = pController->AcknowledgedPawn;
				SDK::TArray<SDK::UObject*>& aActors = pPawns->Objects;
				for (int iActor = 0; iActor < aActors.Num(); ++iActor)
				{
					auto* pActor = static_cast<SDK::AActor*>(aActors[iActor]);
					if (!Payday3::IsAliveActor(pActor) || pActor == pLocalPawn || Payday3::IsDrone(pActor))
						continue;
					SDK::USkeletalMeshComponent* pMesh = Payday3::GetPawnMesh(pActor);
					const float flDistance = DistanceMeters(pActor, vecCamera);
					if (pMesh && flDistance <= *m_pDebugBoneDistance)
						aTargets.push_back({ pMesh, flDistance });
				}
				std::ranges::sort(aTargets, {}, &stDebugBoneTarget::flDistance);
				if (aTargets.size() > kDebugBoneTargets)
					aTargets.resize(kDebugBoneTargets);

				for (const stDebugBoneTarget& rTarget : aTargets)
					DrawDebugSkeleton(rList, pController, rTarget.pMesh);
			}

			void DrawDebugSkeleton(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::USkeletalMeshComponent* pMesh)
			{
				constexpr OmegaGUI::color colLine(255, 0, 255);
				constexpr OmegaGUI::color colText(0, 255, 0);
				const stDebugBones& rBones = DebugBones(pMesh);
				const std::size_t uCount = rBones.aNames.size();
				const bool bLines = *m_pDebugBoneLines;
				const bool bIndices = *m_pDebugBoneIndices;
				const bool bNames = *m_pDebugBoneNames;
				if (uCount < 2 || (!bLines && !bIndices && !bNames))
					return;

				m_aDebugScreen.assign(uCount, std::nullopt);
				for (std::size_t uBone = 1; uBone < uCount; ++uBone)
					m_aDebugScreen[uBone] = Project(pController, pMesh->GetSocketLocation(rBones.aNames[uBone]));

				for (std::size_t uBone = 1; uBone < uCount; ++uBone)
				{
					const std::optional<OmegaGUI::vec2>& optBone = m_aDebugScreen[uBone];
					if (!optBone)
						continue;
					const int iParent = rBones.aParents[uBone];
					if (bLines && iParent > 0 && m_aDebugScreen[iParent])
					{
						rList.AddLine(*m_aDebugScreen[iParent] - OmegaGUI::vec2{ 1.f, 1.f }, *optBone - OmegaGUI::vec2{ 1.f, 1.f }, kOutline, 3.f);
						rList.AddLine(*m_aDebugScreen[iParent], *optBone, colLine, 1.5f);
					}
					if (bIndices || bNames)
						DrawOutlinedText(rList, *optBone, colText, bIndices && bNames ? rBones.aBothText[uBone] : (bNames ? rBones.aNameText[uBone] : rBones.aIndexText[uBone]));
				}
			}

#endif
			[[nodiscard]] bool CamerasShown() const
			{
				const stCategory& rEnemies = Category(ECategory::Enemies);
				return stCategory::On(rEnemies.pEnabled) && *m_pCameras && (*rEnemies.pBox || *rEnemies.pName || *rEnemies.pDistance);
			}

			[[nodiscard]] bool ItemOutlinesWanted() const
			{
				return !DebugOn() && ((*m_pCameras && Category(ECategory::Enemies).OutlineOn()) || Category(ECategory::Loot).OutlineOn() || Category(ECategory::KeyItems).OutlineOn() || Category(ECategory::Objectives).OutlineOn() || Category(ECategory::Supplies).OutlineOn());
			}

			[[nodiscard]] bool ThingsShown() const
			{
				return !DebugOn() && (CamerasShown() || Category(ECategory::Loot).AnyDrawn() || Category(ECategory::KeyItems).AnyDrawn() || Category(ECategory::Objectives).AnyDrawn() || Category(ECategory::Supplies).AnyDrawn());
			}

			void ScanThings()
			{
				const Profiler::clScope clProfile("visuals.scan things");
				using Clock_t = std::chrono::steady_clock;
				{
					SDK::UWorld* pHeldWorld = Unreal::GetWorld();
					std::scoped_lock lckHeld(m_mtxHeldKeys);
					if (pHeldWorld != m_pHeldWorld)
					{
						m_pHeldWorld = pHeldWorld;
						m_mapHeldKeys.clear();
					}
				}
				if (!(ThingsShown() || ItemOutlinesWanted()) || !Payday3::IsReady())
				{
					std::scoped_lock lckThings(m_mtxThings);
					m_pThings.reset();
					return;
				}
				if (Clock_t::now() < m_tpNextThingScan)
					return;
				m_tpNextThingScan = Clock_t::now() + kThingScanInterval;

				auto pSnapshot = std::make_shared<stThingSnapshot>();
				pSnapshot->pWorld = Unreal::GetWorld();
				Payday3::ForEachLevelActor(pSnapshot->pWorld, [&](SDK::AActor* pActor) {
					if (!Payday3::IsAliveActor(pActor))
						return;
					if (pActor->IsA(SDK::ASBZSecurityCamera::StaticClass()))
					{
						pSnapshot->aThings.push_back({ pActor, pActor->Class, EThingKind::Camera, {} });
						return;
					}
					if (Payday3::AsDropOff(pActor))
					{
						pSnapshot->aThings.push_back({ pActor, pActor->Class, EThingKind::DropOff, {} });
						return;
					}
					if (const std::uint8_t uCrate = pActor->Class ? CrateFlags(pActor->Class) : std::uint8_t{ 0 }; (uCrate & 1) != 0)
					{
						static constexpr Payday3::stItemInfo kCrate{ "Crate", Payday3::EItemType::Container, Payday3::EItemCategory::KeycardReader };
						auto* pCrate = static_cast<SDK::ABP_WoodenCrate_C*>(pActor);
						if (!IsUsableInteractable(pCrate->SBZInteractable))
							return;
						std::string strLabel = EspText("Unopened crate");
						if (Unreal::IsNetHost() && (uCrate & 2) != 0)
						{
							SDK::TArray<SDK::AActor*>& aInside = static_cast<SDK::ABP_WoodenCrate_SatellitePart_LowValue_C*>(pActor)->actorsInside;
							std::string strContents;
							for (int iInside = 0; iInside < aInside.Num(); ++iInside)
							{
								SDK::AActor* pInside = aInside[iInside];
								if (!Payday3::IsAliveActor(pInside))
									continue;
								const Payday3::stItemInfo& rInside = Payday3::ResolveItem(pInside);
								if (rInside.eType == Payday3::EItemType::None || strContents.find(EspText(rInside.strLabel)) != std::string::npos)
									continue;
								strContents += (strContents.empty() ? "" : ", ") + EspText(rInside.strLabel);
							}
							if (!strContents.empty())
								strLabel = EspText("Crate") + ": " + strContents;
						}
						pSnapshot->aThings.push_back({ pActor, pActor->Class, EThingKind::Item, kCrate, std::move(strLabel) });
						return;
					}
					if (pActor->IsA(SDK::ASBZItemContainer::StaticClass()))
					{
						static constexpr Payday3::stItemInfo kContainer{ "Container", Payday3::EItemType::Container, Payday3::EItemCategory::KeycardReader };
						auto* pContainer = static_cast<SDK::ASBZItemContainer*>(pActor);
						if (!IsUsableInteractable(pContainer->InteractableComponent))
							return;
						std::string strLabel = EspText("Unopened container");
						if (Unreal::IsNetHost())
						{
							SDK::AActor* pHidden = pContainer->HiddenItem;
							if (!pContainer->bHasItem || !Payday3::IsAliveActor(pHidden))
								return;
							const Payday3::stItemInfo& rHidden = Payday3::ResolveItem(pHidden);
							strLabel = std::format("{}: {}", EspText("Container"), rHidden.eType != Payday3::EItemType::None ? EspText(rHidden.strLabel) : (pHidden->Class ? pHidden->Class->GetName() : std::string("?")));
						}
						pSnapshot->aThings.push_back({ pActor, pActor->Class, EThingKind::Item, kContainer, std::move(strLabel) });
						return;
					}
					if (pActor->IsA(SDK::ASBZWaypoint::StaticClass()))
					{
						static constexpr Payday3::stItemInfo kLabWaypoint{ "Lab", Payday3::EItemType::Lab, Payday3::EItemCategory::KeycardReader };
						if (pActor->GetName().starts_with("SBZWaypoint_Lab"))
							pSnapshot->aThings.push_back({ pActor, pActor->Class, EThingKind::Item, kLabWaypoint });
						return;
					}
					const Payday3::stItemInfo& rItem = Payday3::ResolveItem(pActor);
					if (rItem.eType != Payday3::EItemType::None)
						pSnapshot->aThings.push_back({ pActor, pActor->Class, EThingKind::Item, rItem });
				});

				std::scoped_lock lckThings(m_mtxThings);
				m_pThings = std::move(pSnapshot);
			}

			void DrawThings(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::UWorld* pWorld, const SDK::FVector& vecCamera, bool bCameras)
			{
				const Profiler::clScope clProfile("visuals.draw things");
				std::shared_ptr<const stThingSnapshot> pSnapshot;
				{
					std::scoped_lock lckThings(m_mtxThings);
					pSnapshot = m_pThings;
				}
				if (!pSnapshot || pSnapshot->pWorld != pWorld)
					return;

				const stCategory& rObjectives = Category(ECategory::Objectives);
				const bool bDropOffs = stCategory::On(rObjectives.pEnabled) && *m_pShowDropOffs && (*rObjectives.pName || *rObjectives.pDistance);
				for (const stThing& rThing : pSnapshot->aThings)
				{
					SDK::AActor* pActor = rThing.pActor;
					if (!Payday3::IsAliveActor(pActor) || pActor->Class != rThing.pClass)
						continue;

					switch (rThing.eKind)
					{
					case EThingKind::Camera:
						if (bCameras)
							DrawCamera(rList, pController, static_cast<SDK::ASBZSecurityCamera*>(pActor), vecCamera);
						break;
					case EThingKind::DropOff:
						if (bDropOffs)
							DrawDropOff(rList, pController, static_cast<SDK::ASBZBagTriggerVolume*>(pActor), vecCamera);
						break;
					case EThingKind::Item:
						DrawItem(rList, pController, pActor, rThing.stItem, rThing.strLabel, vecCamera);
						break;
					}
				}
			}

			void DrawItem(OmegaGUI::clDrawList& rList, SDK::APlayerController* pController, SDK::AActor* pActor, const Payday3::stItemInfo& rItem, std::string_view strLabelOverride, const SDK::FVector& vecCamera) const
			{
				const stItemSetting* pSetting = Setting(rItem.eType);
				if (!pSetting || !*pSetting->pShow)
					return;
				const stCategory& rCategory = Category(CategoryOf(rItem.eType));
				if (!stCategory::On(rCategory.pEnabled) || (!*rCategory.pName && !*rCategory.pDistance))
					return;
				if (IsDepleted(rItem.eType, pActor) || HoldsKeyFor(rItem))
					return;

				const SDK::FVector vecActor = Payday3::GetActorLocation(pActor);
				const std::optional<OmegaGUI::vec2> optScreen = Project(pController, vecActor);
				if (!optScreen)
					return;
				const std::string_view strLabel = strLabelOverride.empty() ? std::string_view(EspText(rItem.strLabel)) : strLabelOverride;
				const float flLineHeight = OmegaGUI::CalcTextSize(strLabel).y;
				DrawNameAndDistance(rList, optScreen->x, optScreen->y + flLineHeight * .5f, *rCategory.pName ? strLabel : std::string_view(), *pSetting->pColor, *rCategory.pDistance ? FormatDistance(DistanceMeters(vecActor, vecCamera)) : std::string(), *pSetting->pColor);
			}

#ifdef _DEBUG
			bool* m_pDebugEnabled = nullptr;
			bool* m_pDebugActors = nullptr;
			bool* m_pDebugHierarchy = nullptr;
			float* m_pDebugActorDistance = nullptr;
			bool* m_pDebugBones = nullptr;
			bool* m_pDebugBoneLines = nullptr;
			bool* m_pDebugBoneIndices = nullptr;
			bool* m_pDebugBoneNames = nullptr;
			float* m_pDebugBoneDistance = nullptr;
			SDK::UWorld* m_pDebugWorld = nullptr;
			std::vector<SDK::AActor*> m_aDebugActors;
			std::vector<SDK::AActor*> m_aDebugNear;
			std::vector<SDK::AActor*> m_aDebugNearNext;
			std::size_t m_uDebugCursor = 0;
			std::chrono::steady_clock::time_point m_tpDebugCycle;
			std::unordered_map<SDK::UClass*, std::vector<std::string>> m_mapDebugClasses;
			std::unordered_map<SDK::USkeletalMeshComponent*, stDebugBones> m_mapDebugBones;
			std::vector<std::optional<OmegaGUI::vec2>> m_aDebugScreen;
#endif
			std::array<stCategory, 7> m_aCategories = { {
				{ "enemies", "visuals.tab_enemies", true },
				{ "civilians", "visuals.tab_civilians", true },
				{ "players", "visuals.tab_players", true },
				{ "loot", "visuals.loot", false },
				{ "key_items", "visuals.items", false },
				{ "objectives", "visuals.tab_objectives", false },
				{ "supplies", "visuals.supplies", false },
			} };
			bool* m_pCameras = nullptr;
			bool* m_pOmegaTag = nullptr;
			bool* m_pAimbotTarget = nullptr;
			OmegaGUI::color* m_pAimbotTargetColor = nullptr;
			stColorPair m_stBoxColors;
			stColorPair m_stNameColors;
			stColorPair m_stDistanceColors;
			stColorPair m_stHealthColors;
			OmegaGUI::color* m_pArmorColor = nullptr;
			stColorPair m_stSkeletonColors;
			bool* m_pShowDropOffs = nullptr;
			bool* m_pShowVips = nullptr;
			OmegaGUI::color* m_pVipColor = nullptr;
			stCategory m_stVip{ "vip", "", true };
			OmegaGUI::color* m_pDropOffColor = nullptr;
			std::array<stItemSetting, 26> m_aItems = { {
				{ Payday3::EItemType::Cash, "visuals.show_cash", "visuals.item_cash_color", OmegaGUI::color(0, 255, 255) },
				{ Payday3::EItemType::MoneyMedium, "visuals.show_money_medium", "visuals.item_money_medium_color", OmegaGUI::color(120, 220, 120) },
				{ Payday3::EItemType::MoneyLarge, "visuals.show_money_large", "visuals.item_money_large_color", OmegaGUI::color(0, 255, 0) },
				{ Payday3::EItemType::Jewelry, "visuals.show_jewelry", "visuals.item_jewelry_color", OmegaGUI::color(255, 0, 255) },
				{ Payday3::EItemType::RareMinerals, "visuals.show_rare_minerals", "visuals.item_rare_minerals_color", OmegaGUI::color(255, 90, 200) },
				{ Payday3::EItemType::RareStone, "visuals.show_rare_stones", "visuals.item_rare_stones_color", OmegaGUI::color(255, 150, 90) },
				{ Payday3::EItemType::Container, "visuals.show_containers", "visuals.item_containers_color", OmegaGUI::color(200, 170, 110) },
				{ Payday3::EItemType::CigarBox, "visuals.show_cigar_boxes", "visuals.item_cigar_boxes_color", OmegaGUI::color(190, 120, 60) },
				{ Payday3::EItemType::SatellitePart, "visuals.show_satellite_parts", "visuals.item_satellite_parts_color", OmegaGUI::color(120, 200, 255) },
				{ Payday3::EItemType::Cocaine, "visuals.show_cocaine", "visuals.item_cocaine_color", OmegaGUI::color(240, 240, 255) },
				{ Payday3::EItemType::ATM, "visuals.show_atms", "visuals.item_atm_color", OmegaGUI::color(80, 200, 255) },
				{ Payday3::EItemType::DepositBox, "visuals.show_deposit_boxes", "visuals.item_deposit_box_color", OmegaGUI::color(255, 0, 0) },
				{ Payday3::EItemType::Keycard, "visuals.show_keycards", "visuals.item_keycard_color", OmegaGUI::color(0, 0, 255) },
				{ Payday3::EItemType::QRPhone, "visuals.show_qr_phones", "visuals.item_qr_phone_color", OmegaGUI::color(255, 255, 0) },
				{ Payday3::EItemType::USB, "visuals.show_usbs", "visuals.item_usb_color", OmegaGUI::color(255, 128, 0) },
				{ Payday3::EItemType::RFIDTag, "visuals.show_rfid_tags", "visuals.item_rfid_tag_color", OmegaGUI::color(0, 200, 255) },
				{ Payday3::EItemType::Lab, "visuals.show_lab", "visuals.item_lab_color", OmegaGUI::color(255, 120, 40) },
				{ Payday3::EItemType::KeycardReader, "visuals.show_keycard_readers", "visuals.item_keycard_reader_color", OmegaGUI::color(255, 128, 128) },
				{ Payday3::EItemType::Plank, "visuals.show_planks", "visuals.item_plank_color", OmegaGUI::color(200, 150, 90) },
				{ Payday3::EItemType::HydrogenChloride, "visuals.show_hydrogen_chloride", "visuals.item_hydrogen_chloride_color", OmegaGUI::color(120, 255, 200) },
				{ Payday3::EItemType::MuriaticAcid, "visuals.show_muriatic_acid", "visuals.item_muriatic_acid_color", OmegaGUI::color(255, 230, 80) },
				{ Payday3::EItemType::CausticSoda, "visuals.show_caustic_soda", "visuals.item_caustic_soda_color", OmegaGUI::color(180, 140, 255) },
				{ Payday3::EItemType::MedkitSmall, "visuals.show_medkit_small", "visuals.item_medkit_small_color", OmegaGUI::color(120, 255, 120) },
				{ Payday3::EItemType::MedkitLarge, "visuals.show_medkit_large", "visuals.item_medkit_large_color", OmegaGUI::color(0, 220, 90) },
				{ Payday3::EItemType::Armor, "visuals.show_armor", "visuals.item_armor_color", OmegaGUI::color(90, 160, 255) },
				{ Payday3::EItemType::Ammo, "visuals.show_ammo", "visuals.item_ammo_color", OmegaGUI::color(255, 220, 60) },
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

			mutable std::mutex m_mtxHeldKeys;
			std::unordered_map<std::string, int> m_mapHeldKeys;
			SDK::UWorld* m_pHeldWorld = nullptr;
			Unreal::HookHandle m_hKeyItems = Unreal::kInvalidHook;
			std::mutex m_mtxThings;
			std::shared_ptr<const stThingSnapshot> m_pThings;
			std::chrono::steady_clock::time_point m_tpNextThingScan;

			SDK::UWorld* m_pLastWorld = nullptr;
			std::chrono::steady_clock::time_point m_tpBonesPruned;
			std::unordered_map<SDK::USkeletalMeshComponent*, BoneCache_t> m_mapBones;
		};

		clVisualsFeature g_clVisualsFeature;
	}
}

#endif
