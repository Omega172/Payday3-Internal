#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include <Windows.h>

#include "Config/Config.hpp"
#include "Core/Log.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Features/Feature.hpp"
#include "Hooks/Hook.hpp"
#include "Memory/Memory.hpp"
#include "Memory/Pattern.hpp"
#include "Memory/Safety.hpp"
#include "Menu/Widgets.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma intrinsic(_ReturnAddress)

namespace OmegaWare
{
	namespace
	{
		constexpr std::size_t kGetPlayerViewPointIndex = 0x106;
		constexpr std::uint64_t kSilentLockCalls = 2000;
		constexpr std::uint64_t kAutoFireIdleLimit = 300;

		using FnGetPlayerViewPoint = void (*)(SDK::APlayerController*, SDK::FVector*, SDK::FRotator*);

		constexpr std::array<const wchar_t*, 7> g_aBones = { L"Head", L"Spine3", L"LeftForeArm", L"RightForeArm", L"Hips", L"LeftLeg", L"RightLeg" };

		struct stTarget
		{
			SDK::ASBZCharacter* pCharacter = nullptr;
			SDK::FVector vecAimWorld;
			float flScreenDistance = 0.f;
		};

		struct stSilentState
		{
			std::mutex mtxState;
			bool bOverride = false;
			bool bManualFire = false;
			SDK::FVector vecLocation;
			SDK::FRotator rotRotation;

			std::vector<std::pair<std::uintptr_t, std::uint64_t>> aReturnCounts;
			std::uintptr_t uFireReturn1 = 0;
			std::uintptr_t uFireReturn2 = 0;
			bool bReady = false;
			std::vector<std::uintptr_t> aAutoFireReturns;
			std::unordered_map<std::uintptr_t, std::uint64_t> mapIdleCounts;
		};

		thread_local bool t_bInAutoFire = false;

		stSilentState g_stSilent;
		Hooks::clHook<FnGetPlayerViewPoint> g_clGetPlayerViewPoint("APlayerController::GetPlayerViewPoint");

		float NormalizeAngle(float flAngle)
		{
			flAngle = std::fmod(flAngle + 180.f, 360.f);
			if (flAngle < 0.f)
				flAngle += 360.f;
			return flAngle - 180.f;
		}

		void ResetSilentCalibration()
		{
			std::scoped_lock lckState(g_stSilent.mtxState);
			g_stSilent.aReturnCounts.clear();
			g_stSilent.uFireReturn1 = 0;
			g_stSilent.uFireReturn2 = 0;
			g_stSilent.bReady = false;
			g_stSilent.aAutoFireReturns.clear();
			g_stSilent.mapIdleCounts.clear();
		}

		void LearnFirePath(std::uintptr_t uReturnAddress)
		{
			if (g_stSilent.bReady)
				return;

			auto itEntry = std::ranges::find(g_stSilent.aReturnCounts, uReturnAddress, &std::pair<std::uintptr_t, std::uint64_t>::first);
			if (itEntry != g_stSilent.aReturnCounts.end())
				++itEntry->second;
			else
				g_stSilent.aReturnCounts.emplace_back(uReturnAddress, 1ull);

			if (g_stSilent.aReturnCounts.size() <= 5)
				return;

			std::array<std::pair<std::uintptr_t, std::uint64_t>, 3> aLowest{};
			for (const auto& stEntry : g_stSilent.aReturnCounts)
			{
				if (stEntry.second < aLowest[0].second || !aLowest[0].first)
				{
					aLowest[2] = aLowest[1];
					aLowest[1] = aLowest[0];
					aLowest[0] = stEntry;
				}
				else if (stEntry.second < aLowest[1].second || !aLowest[1].first)
				{
					aLowest[2] = aLowest[1];
					aLowest[1] = stEntry;
				}
				else if (stEntry.second < aLowest[2].second || !aLowest[2].first)
					aLowest[2] = stEntry;
			}

			if (aLowest[2].second > kSilentLockCalls && aLowest[1].second >= aLowest[0].second && (aLowest[1].second - aLowest[0].second) < (aLowest[2].second - aLowest[1].second))
			{
				g_stSilent.uFireReturn1 = aLowest[0].first;
				g_stSilent.uFireReturn2 = aLowest[1].first;
				g_stSilent.bReady = true;
				g_stSilent.aReturnCounts.clear();
				LogInfo("silent aim ready, fire path {:#x} / {:#x}", g_stSilent.uFireReturn1, g_stSilent.uFireReturn2);
			}
		}

		void HkGetPlayerViewPoint(SDK::APlayerController* pController, SDK::FVector* pLocation, SDK::FRotator* pRotation)
		{
			Hooks::clCallGuard clGuard;
			g_clGetPlayerViewPoint(pController, pLocation, pRotation);
			if (!pLocation || !pRotation)
				return;

			const auto uReturnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			std::scoped_lock lckState(g_stSilent.mtxState);
			std::vector<std::uintptr_t>& aAutoFire = g_stSilent.aAutoFireReturns;
			if (!g_stSilent.bOverride)
				++g_stSilent.mapIdleCounts[uReturnAddress];
			auto fnIdleCount = [](std::uintptr_t uAddress) {
				const auto itCount = g_stSilent.mapIdleCounts.find(uAddress);
				return itCount != g_stSilent.mapIdleCounts.end() ? itCount->second : 0ull;
			};
			if (t_bInAutoFire && aAutoFire.size() < 8 && std::ranges::find(aAutoFire, uReturnAddress) == aAutoFire.end() && fnIdleCount(uReturnAddress) < kAutoFireIdleLimit)
			{
				aAutoFire.push_back(uReturnAddress);
				LogInfo("silent aim learned auto fire path {:#x}", uReturnAddress);
			}
			if (std::erase_if(aAutoFire, [&](std::uintptr_t uAddress) { return fnIdleCount(uAddress) >= kAutoFireIdleLimit; }) > 0)
				LogInfo("silent aim dropped an auto fire path that is also used outside firing");
			const bool bAutoFirePath = std::ranges::find(aAutoFire, uReturnAddress) != aAutoFire.end();
			if (!bAutoFirePath)
				LearnFirePath(uReturnAddress);
			if (!g_stSilent.bOverride)
				return;
			const bool bLearnedFirePath = g_stSilent.bReady && (uReturnAddress == g_stSilent.uFireReturn1 || uReturnAddress == g_stSilent.uFireReturn2);
			const bool bManualWhileLearning = g_stSilent.bManualFire && !g_stSilent.bReady;
			if (!bAutoFirePath && !bLearnedFirePath && !bManualWhileLearning)
				return;
			*pLocation = g_stSilent.vecLocation;
			*pRotation = g_stSilent.rotRotation;
		}

		using TryActivateAbility_t = bool (*)(SDK::USBZPlayerAbilitySystemComponent*, std::uint32_t, bool);

		[[nodiscard]] TryActivateAbility_t ResolveTryActivateAbility()
		{
			static const TryActivateAbility_t s_fnTryActivate = [] {
				constexpr auto aPattern = Memory::Pattern<"48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC ? 48 83 65 ? 00 45 8A E0 83 65 ? 00 41 B0">;
				const std::optional<Memory::stModule> optModule = Memory::GetModule();
				const std::uintptr_t uAddress = optModule ? Memory::FindPattern(*optModule, aPattern) : 0;
				if (uAddress)
					LogInfo("USBZPlayerAbilitySystemComponent::TryActivateAbility found at {:#x}", uAddress - optModule->uBase);
				else
					LogError("USBZPlayerAbilitySystemComponent::TryActivateAbility pattern not found, auto fire is disabled");
				return reinterpret_cast<TryActivateAbility_t>(uAddress);
			}();
			return s_fnTryActivate;
		}

		[[nodiscard]] bool IsLoud()
		{
			SDK::UWorld* pWorld = Unreal::GetWorld();
			if (!pWorld || !Payday3::IsAlive(pWorld->GameState) || !pWorld->GameState->IsA(SDK::APD3HeistGameState::StaticClass()))
				return true;
			return static_cast<SDK::APD3HeistGameState*>(pWorld->GameState)->CurrentHeistState >= SDK::EPD3HeistState::Alarm;
		}

		void ActivateFireAbility()
		{
			const TryActivateAbility_t fnTryActivate = ResolveTryActivateAbility();
			SDK::APlayerController* pController = Unreal::GetPlayerController();
			if (!fnTryActivate || !pController || !Payday3::IsAlive(pController->PlayerState) || !pController->PlayerState->IsA(SDK::ASBZPlayerState::StaticClass()))
				return;
			SDK::USBZPlayerAbilitySystemComponent* pAbilities = static_cast<SDK::ASBZPlayerState*>(pController->PlayerState)->AbilitySystem;
			if (!Payday3::IsAlive(pAbilities))
				return;
			for (SDK::FGameplayAbilitySpec& rSpec : pAbilities->ActivatableAbilities.Items)
			{
				if (!Payday3::IsAlive(rSpec.Ability))
					continue;
				if (!rSpec.Ability->IsA(SDK::USBZPlayerFireRangedWeaponAbility::StaticClass()))
					continue;
				t_bInAutoFire = true;
				fnTryActivate(pAbilities, static_cast<std::uint32_t>(rSpec.Handle.Handle), true);
				t_bInAutoFire = false;
			}
		}

		void SetOverride(bool bOverride, bool bManualFire = false, const SDK::FVector& vecLocation = {}, const SDK::FRotator& rotRotation = {})
		{
			std::scoped_lock lckState(g_stSilent.mtxState);
			g_stSilent.bOverride = bOverride;
			g_stSilent.bManualFire = bOverride && bManualFire;
			if (bOverride)
			{
				g_stSilent.vecLocation = vecLocation;
				g_stSilent.rotRotation = rotRotation;
			}
		}

		class clAimbotFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "aimbot"; }

			bool Setup() override
			{
				m_pEnabled = &Config::Add("aimbot.enabled", false);
				m_pVisibleCheck = &Config::Add("aimbot.visible_check", false);
				m_pDrawFov = &Config::Add("aimbot.draw_fov", false);
				m_pKey = &Config::Add("aimbot.key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				m_pType = &Config::Add("aimbot.type", 0);
				m_pTarget = &Config::Add("aimbot.target", 0);
				m_pFovColor = &Config::Add("aimbot.fov_color", OmegaGUI::color(255, 0, 0));
				m_pFov = &Config::Add("aimbot.fov", 60);
				m_pSmoothing = &Config::Add("aimbot.smoothing", 10);
				m_pAutoFire = &Config::Add("aimbot.auto_fire", false);
				m_pAutoFireLoudOnly = &Config::Add("aimbot.auto_fire_loud_only", true);
				return true;
			}

			void Run() override
			{
				if (g_clGetPlayerViewPoint.IsInstalled() || !Payday3::IsReady())
					return;
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				if (!pController || !pController->AcknowledgedPawn || pController == m_pHookAttempt)
					return;
				m_pHookAttempt = pController;
				if (!Memory::GetVirtualMethod(pController, kGetPlayerViewPointIndex))
				{
					LogError("GetPlayerViewPoint vtable slot {:#x} is empty", kGetPlayerViewPointIndex);
					return;
				}
				ResetSilentCalibration();
				if (g_clGetPlayerViewPoint.InstallVirtual(pController, kGetPlayerViewPointIndex, &HkGetPlayerViewPoint))
					LogInfo("silent aim hook installed at vtable slot {:#x}", kGetPlayerViewPointIndex);
			}

			void Destroy() override
			{
				SetOverride(false);
				g_clGetPlayerViewPoint.Remove();
				ResetSilentCalibration();
				m_pHookAttempt = nullptr;
			}

			void Render() override
			{
				if (*m_pDrawFov)
					OmegaGUI::GetBackgroundDrawList().AddCircle((OmegaGUI::GetDisplaySize() * .5f).Floor(), static_cast<float>(*m_pFov), *m_pFovColor, 100, 1.f);
				UpdateAim();
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				OmegaGUI::BeginColumns(2);
				if (OmegaGUI::BeginSection("aimbot.section_general"_l))
				{
					OmegaGUI::Checkbox("aimbot.enabled"_l, m_pEnabled);
					OmegaGUI::Checkbox("aimbot.visible_check"_l, m_pVisibleCheck);
					OmegaGUI::Checkbox("aimbot.draw_fov"_l, m_pDrawFov);
					OmegaGUI::Checkbox("aimbot.auto_fire"_l, m_pAutoFire);
					OmegaGUI::ItemTooltip("aimbot.auto_fire_hint"_t);
					if (*m_pAutoFire)
					{
						OmegaGUI::Indent();
						OmegaGUI::Checkbox("aimbot.auto_fire_loud_only"_l, m_pAutoFireLoudOnly);
						OmegaGUI::ItemTooltip("aimbot.auto_fire_loud_only_hint"_t);
						OmegaGUI::Unindent();
					}
					OmegaGUI::Spacing();
					OmegaGUI::TextWrapped(OmegaLocalization::Format("aimbot.silent_status", SilentStatus()));
					OmegaGUI::EndSection();
				}
				OmegaGUI::NextColumn();
				if (OmegaGUI::BeginSection("aimbot.section_settings"_l))
				{
					Widgets::Keybind("aimbot.key"_l, *m_pKey);
					OmegaGUI::TextMuted("aimbot.key_hint"_t);
					const std::array<std::string_view, 2> aTypes = { "aimbot.type_silent"_t, "aimbot.type_snapping"_t };
					OmegaGUI::Combo("aimbot.type"_l, m_pType, aTypes);
					const std::array<std::string_view, 7> aBones = { "aimbot.bone_head"_t, "aimbot.bone_torso"_t, "aimbot.bone_left_arm"_t, "aimbot.bone_right_arm"_t, "aimbot.bone_hips"_t, "aimbot.bone_left_leg"_t, "aimbot.bone_right_leg"_t };
					OmegaGUI::Combo("aimbot.target"_l, m_pTarget, aBones);
					OmegaGUI::ColorEdit("aimbot.fov_color"_l, m_pFovColor);
					OmegaGUI::SliderInt("aimbot.fov"_l, m_pFov, 0, 200);
					OmegaGUI::SliderInt("aimbot.smoothing"_l, m_pSmoothing, 0, 100);
					OmegaGUI::EndSection();
				}
				OmegaGUI::EndColumns();
			}

		private:
			[[nodiscard]] static std::string_view SilentStatus()
			{
				using namespace OmegaLocalization::Literals;
				if (!g_clGetPlayerViewPoint.IsInstalled())
					return "aimbot.silent_no_hook"_t;
				std::scoped_lock lckState(g_stSilent.mtxState);
				return g_stSilent.bReady ? "aimbot.silent_ready"_t : "aimbot.silent_learning"_t;
			}

			void UpdateAim()
			{
				const bool bKeyHeld = m_pKey->uKey == 0 || m_pKey->bActive;
				if (!*m_pEnabled || !bKeyHeld || !Payday3::IsReady())
				{
					SetOverride(false);
					return;
				}

				SDK::APlayerController* pController = Unreal::GetPlayerController();
				SDK::APlayerCameraManager* pCamera = pController ? pController->PlayerCameraManager : nullptr;
				if (!Memory::IsValidObjectPtr(pCamera))
				{
					SetOverride(false);
					return;
				}

				const SDK::FVector vecCamera = pCamera->GetCameraLocation();
				const SDK::FRotator rotCamera = pCamera->GetCameraRotation();
				const float flMaxDistance = *m_pDrawFov ? static_cast<float>(*m_pFov) : std::numeric_limits<float>::max();
				const int iBone = std::clamp(*m_pTarget, 0, static_cast<int>(g_aBones.size()) - 1);

				const std::optional<stTarget> optTarget = SelectTarget(pController, vecCamera, flMaxDistance, g_aBones[iBone]);
				if (!optTarget)
				{
					SetOverride(false);
					return;
				}

				const SDK::FRotator rotDesired = SDK::UKismetMathLibrary::FindLookAtRotation(vecCamera, optTarget->vecAimWorld);
				const bool bAutoFire = *m_pAutoFire && (!*m_pAutoFireLoudOnly || IsLoud());
				if (bAutoFire)
					QueueAutoFire();
				if (*m_pType == 0)
				{
					const bool bManualFire = !OmegaGUI::WantsMouse() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
					SetOverride(bAutoFire || bManualFire, bManualFire, optTarget->vecAimWorld, rotDesired);
					return;
				}

				SetOverride(false);
				if (m_bInputQueued.exchange(true))
					return;
				const float flAlpha = 1.f / static_cast<float>(std::max(1, *m_pSmoothing));
				const float flYaw = NormalizeAngle(static_cast<float>(rotDesired.Yaw - rotCamera.Yaw)) * flAlpha;
				const float flPitch = -NormalizeAngle(static_cast<float>(rotDesired.Pitch - rotCamera.Pitch)) * flAlpha;
				if (!Unreal::RunOnGameThread([this, flYaw, flPitch] {
						m_bInputQueued = false;
						if (SDK::APlayerController* pGameController = IsActive() ? Unreal::GetPlayerController() : nullptr)
						{
							pGameController->AddYawInput(flYaw);
							pGameController->AddPitchInput(flPitch);
						}
					}))
					m_bInputQueued = false;
			}

			void QueueAutoFire()
			{
				if (m_bFireQueued.exchange(true))
					return;
				if (!Unreal::RunOnGameThread([this] {
						m_bFireQueued = false;
						if (IsActive() && *m_pAutoFire)
							ActivateFireAbility();
					}))
					m_bFireQueued = false;
			}

			std::optional<stTarget> SelectTarget(SDK::APlayerController* pController, const SDK::FVector& vecCamera, float flMaxDistance, const wchar_t* wszBone) const
			{
				SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns();
				if (!pPawns)
					return std::nullopt;

				int iWidth = 0;
				int iHeight = 0;
				pController->GetViewportSize(&iWidth, &iHeight);
				if (iWidth <= 0 || iHeight <= 0)
					return std::nullopt;

				const float flCenterX = static_cast<float>(iWidth) * .5f;
				const float flCenterY = static_cast<float>(iHeight) * .5f;
				const SDK::FName nameBone = Payday3::MakeName(wszBone);
				SDK::APawn* pLocalPawn = pController->AcknowledgedPawn;

				std::optional<stTarget> optBest;
				float flBest = flMaxDistance;
				SDK::TArray<SDK::UObject*>& aActors = pPawns->Objects;
				for (int iActor = 0; iActor < aActors.Num(); ++iActor)
				{
					auto* pActor = static_cast<SDK::AActor*>(aActors[iActor]);
					if (!Memory::IsValidObjectPtr(pActor) || pActor == pLocalPawn)
						continue;
					auto* pCharacter = static_cast<SDK::ASBZCharacter*>(pActor);
					if (!pCharacter->bIsAlive || !Memory::IsValidObjectPtr(pCharacter->Mesh))
						continue;

					if (Payday3::GetEnemyInfo(Payday3::ResolveEnemyType(pActor)).eCategory != Payday3::EEnemyCategory::Cop)
						continue;
					if (*m_pVisibleCheck && !pController->LineOfSightTo(pCharacter, vecCamera, false))
						continue;

					const SDK::FVector vecAim = pCharacter->Mesh->GetSocketLocation(nameBone);
					SDK::FVector2D vecScreen;
					if (!pController->ProjectWorldLocationToScreen(vecAim, &vecScreen, false))
						continue;

					const float flDistance = static_cast<float>(std::hypot(vecScreen.X - flCenterX, vecScreen.Y - flCenterY));
					if (flDistance > flBest)
						continue;
					flBest = flDistance;
					optBest = stTarget{ pCharacter, vecAim, flDistance };
				}
				return optBest;
			}

			bool* m_pEnabled = nullptr;
			bool* m_pVisibleCheck = nullptr;
			bool* m_pDrawFov = nullptr;
			Config::stKeybind* m_pKey = nullptr;
			int* m_pType = nullptr;
			int* m_pTarget = nullptr;
			OmegaGUI::color* m_pFovColor = nullptr;
			int* m_pFov = nullptr;
			int* m_pSmoothing = nullptr;
			bool* m_pAutoFire = nullptr;
			bool* m_pAutoFireLoudOnly = nullptr;
			std::atomic<bool> m_bFireQueued = false;
			SDK::APlayerController* m_pHookAttempt = nullptr;
			std::atomic<bool> m_bInputQueued = false;
		};

		clAimbotFeature g_clAimbotFeature;
	}
}

#endif
