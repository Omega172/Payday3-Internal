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

#include "OmegaRenderHook/Core/Guarded.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <numbers>
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
		constexpr std::size_t kLocalPlayerGetViewPointIndex = 0x5C;
		constexpr std::uint64_t kAutoFireIdleLimit = 300;
		constexpr double kDistanceBucketMeters = 2.0;
		constexpr int kMaxLearnShots = 6;
		constexpr std::size_t kWantedFirePaths = 2;
		constexpr std::int64_t kWarmupMicroseconds = 8'000'000;
		constexpr std::int64_t kMaxIdleGapMicroseconds = 50'000;

		using FnGetPlayerViewPoint = void (*)(SDK::APlayerController*, SDK::FVector*, SDK::FRotator*);
		using FnLocalPlayerGetViewPoint = void (*)(SDK::ULocalPlayer*, SDK::FMinimalViewInfo*);
		using FnFireView = void (*)(void*, void*, SDK::FVector*, SDK::FQuat*);

		constexpr std::array<const wchar_t*, 7> g_aBones = { L"Head", L"Spine3", L"LeftForeArm", L"RightForeArm", L"Hips", L"LeftLeg", L"RightLeg" };

		struct stTarget
		{
			SDK::AActor* pCharacter = nullptr;
			SDK::FVector vecAimWorld;
			float flScore = 0.f;
		};

		struct stSilentState
		{
			std::mutex mtxState;
			bool bOverride = false;
			bool bMagicBullet = true;
			SDK::FVector vecLocation;
			SDK::FRotator rotRotation;
			SDK::FVector vecOriginalLocation;
			SDK::FRotator rotOriginalRotation;
			SDK::FVector vecAppliedLocation;
			SDK::FRotator rotAppliedRotation;
			bool bLeakWatch = false;

			std::vector<std::uintptr_t> aAutoFireReturns;
			std::unordered_map<std::uintptr_t, std::uint64_t> mapIdleCounts;
			std::int64_t llIdleMicroseconds = 0;
			std::chrono::steady_clock::time_point tpLastIdle;
			bool bCalibrated = false;
			bool bFromSignature = false;
		};

		thread_local bool t_bInAutoFire = false;

		stSilentState g_stSilent;
		std::size_t g_uGetPlayerViewPointSlot = kGetPlayerViewPointIndex;
		Hooks::clHook<FnGetPlayerViewPoint> g_clGetPlayerViewPoint("APlayerController::GetPlayerViewPoint");
		Hooks::clHook<FnLocalPlayerGetViewPoint> g_clLocalPlayerGetViewPoint("ULocalPlayer::GetViewPoint");
		Hooks::clHook<FnFireView> g_clFireView("fire view");
		std::size_t g_uFireViewControllerOffset = 0;

		float NormalizeAngle(float flAngle)
		{
			flAngle = std::fmod(flAngle + 180.f, 360.f);
			if (flAngle < 0.f)
				flAngle += 360.f;
			return flAngle - 180.f;
		}

		SDK::FRotator LookAt(const SDK::FVector& vecFrom, const SDK::FVector& vecTo)
		{
			const double dX = vecTo.X - vecFrom.X;
			const double dY = vecTo.Y - vecFrom.Y;
			const double dZ = vecTo.Z - vecFrom.Z;
			SDK::FRotator rotLook;
			rotLook.Yaw = std::atan2(dY, dX) * (180.0 / std::numbers::pi);
			rotLook.Pitch = std::atan2(dZ, std::hypot(dX, dY)) * (180.0 / std::numbers::pi);
			rotLook.Roll = 0.0;
			return rotLook;
		}

		[[nodiscard]] SDK::FQuat QuatFromRotator(const SDK::FRotator& rotRotation)
		{
			constexpr double kHalfRadians = std::numbers::pi / 360.0;
			const double dSinPitch = std::sin(rotRotation.Pitch * kHalfRadians);
			const double dCosPitch = std::cos(rotRotation.Pitch * kHalfRadians);
			const double dSinYaw = std::sin(rotRotation.Yaw * kHalfRadians);
			const double dCosYaw = std::cos(rotRotation.Yaw * kHalfRadians);
			const double dSinRoll = std::sin(rotRotation.Roll * kHalfRadians);
			const double dCosRoll = std::cos(rotRotation.Roll * kHalfRadians);
			SDK::FQuat quatResult{};
			quatResult.X = dCosRoll * dSinPitch * dSinYaw - dSinRoll * dCosPitch * dCosYaw;
			quatResult.Y = -dCosRoll * dSinPitch * dCosYaw - dSinRoll * dCosPitch * dSinYaw;
			quatResult.Z = dCosRoll * dCosPitch * dSinYaw - dSinRoll * dSinPitch * dCosYaw;
			quatResult.W = dCosRoll * dCosPitch * dCosYaw + dSinRoll * dSinPitch * dSinYaw;
			return quatResult;
		}

		[[nodiscard]] std::uintptr_t FindFireView()
		{
			constexpr auto aPattern = Memory::Pattern<"48 8B C4 55 53 56 57 41 54 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC 20 04 00 00 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 90 ? ? ? ? 44 0F 29 A0 ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? 00 00 48 83 B9 ? ? 00 00 00">;
			const std::optional<Memory::stModule> optModule = Memory::GetModule();
			if (!optModule)
				return 0;
			const std::vector<std::uintptr_t> aMatches = Memory::FindAllPatterns(optModule->uBase, optModule->uSize, aPattern);
			if (aMatches.size() != 1)
				return 0;
			const auto* pCode = reinterpret_cast<const std::uint8_t*>(aMatches.front());
			for (std::size_t uByte = 0; uByte + 8 <= 128; ++uByte)
			{
				if (pCode[uByte] == 0x48 && pCode[uByte + 1] == 0x83 && pCode[uByte + 2] == 0xB9 && pCode[uByte + 5] == 0x00 && pCode[uByte + 6] == 0x00 && pCode[uByte + 7] == 0x00)
				{
					g_uFireViewControllerOffset = *reinterpret_cast<const std::uint16_t*>(pCode + uByte + 3);
					return aMatches.front();
				}
			}
			return 0;
		}

		void HkFireView(void* pThis, void* pArgument, SDK::FVector* pLocation, SDK::FQuat* pQuat)
		{
			Hooks::clCallGuard clGuard;
			g_clFireView(pThis, pArgument, pLocation, pQuat);
			if (!pThis || !pLocation || !pQuat)
				return;
			SDK::APlayerController* pLocalController = Unreal::GetPlayerController();
			if (!Memory::IsValidObjectPtr(pLocalController) || *reinterpret_cast<SDK::UObject**>(reinterpret_cast<std::uintptr_t>(pThis) + g_uFireViewControllerOffset) != pLocalController)
				return;
			std::scoped_lock lckState(g_stSilent.mtxState);
			if (!g_stSilent.bOverride)
				return;
			const SDK::FVector vecGameLocation = *pLocation;
			if (g_stSilent.bMagicBullet)
				*pLocation = g_stSilent.vecLocation;
			const SDK::FRotator rotAim = g_stSilent.bMagicBullet ? g_stSilent.rotRotation : LookAt(vecGameLocation, g_stSilent.vecLocation);
			*pQuat = QuatFromRotator(rotAim);
		}

		void ResolveFireCallSites()
		{
			constexpr auto aPattern = Memory::Pattern<"FF 90 ? ? 00 00 48 8B 8E ? ? 00 00 48 8D 55 ? E8">;
			const std::optional<Memory::stModule> optModule = Memory::GetModule();
			g_uGetPlayerViewPointSlot = kGetPlayerViewPointIndex;
			if (!optModule)
				return;
			std::scoped_lock lckState(g_stSilent.mtxState);
			std::size_t uSlot = 0;
			for (const std::uintptr_t uMatch : Memory::FindAllPatterns(optModule->uBase, optModule->uSize, aPattern))
			{
				const std::size_t uDisplacement = *reinterpret_cast<const std::uint16_t*>(uMatch + 2);
				if (uDisplacement == 0 || uDisplacement % sizeof(void*) != 0)
					continue;
				const std::size_t uMatchSlot = uDisplacement / sizeof(void*);
				if (uSlot == 0)
					uSlot = uMatchSlot;
				const std::uintptr_t uReturn = uMatch + 6;
				if (uMatchSlot != uSlot || g_stSilent.aAutoFireReturns.size() >= 8 || std::ranges::find(g_stSilent.aAutoFireReturns, uReturn) != g_stSilent.aAutoFireReturns.end())
					continue;
				g_stSilent.aAutoFireReturns.push_back(uReturn);
				LogInfo("silent aim fire call site found by signature at +{:#x}", uReturn - optModule->uBase);
			}
			if (g_stSilent.aAutoFireReturns.empty())
			{
				LogWarning("silent aim fire call site was not found by signature, it will be learned while you shoot");
				return;
			}
			g_stSilent.bFromSignature = true;
			g_uGetPlayerViewPointSlot = uSlot;
			LogInfo("GetPlayerViewPoint vtable slot {:#x} read from the fire call site{}", uSlot, uSlot == kGetPlayerViewPointIndex ? "" : " (differs from the expected slot)");
		}

		[[nodiscard]] std::uintptr_t FindLocalPlayerGetViewPoint()
		{
			constexpr auto aStart = Memory::Pattern<"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 60 48 8B FA 48 8B F1 E8 ? ? ? ? 48 8D 5F 18">;
			constexpr auto aBody = Memory::Pattern<"48 8D 5F 18 4C 8B C7 4C 8D 77 30 4C 8B CB 48 8B D6 4C 89 74 24 20 48 8B C8 E8 ? ? ? ? 84 C0 75 ? 48 8B 4E 38 48 85 C9 74 ? 48 8B 91 ? ? 00 00">;
			constexpr std::uintptr_t kBodyOffset = 0x24;
			const std::optional<Memory::stModule> optModule = Memory::GetModule();
			if (!optModule)
				return 0;
			const std::vector<std::uintptr_t> aStarts = Memory::FindAllPatterns(optModule->uBase, optModule->uSize, aStart);
			if (aStarts.size() == 1)
				return aStarts.front();
			const std::vector<std::uintptr_t> aBodies = Memory::FindAllPatterns(optModule->uBase, optModule->uSize, aBody);
			return aBodies.size() == 1 ? aBodies.front() - kBodyOffset : 0;
		}

		void ResetSilentCalibration()
		{
			std::scoped_lock lckState(g_stSilent.mtxState);
			g_stSilent.aAutoFireReturns.clear();
			g_stSilent.mapIdleCounts.clear();
			g_stSilent.llIdleMicroseconds = 0;
			g_stSilent.tpLastIdle = {};
			g_stSilent.bCalibrated = false;
			g_stSilent.bFromSignature = false;
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
			{
				++g_stSilent.mapIdleCounts[uReturnAddress];
				const auto tpNow = std::chrono::steady_clock::now();
				if (g_stSilent.tpLastIdle.time_since_epoch().count() != 0)
				{
					const auto llGap = std::chrono::duration_cast<std::chrono::microseconds>(tpNow - g_stSilent.tpLastIdle).count();
					if (llGap < kMaxIdleGapMicroseconds)
						g_stSilent.llIdleMicroseconds += llGap;
				}
				g_stSilent.tpLastIdle = tpNow;
				if (!g_stSilent.bCalibrated && g_stSilent.llIdleMicroseconds >= kWarmupMicroseconds)
				{
					g_stSilent.bCalibrated = true;
					LogInfo("silent aim calibrated, fire paths are learned from now on");
				}
			}
			auto fnIdleCount = [](std::uintptr_t uAddress) {
				const auto itCount = g_stSilent.mapIdleCounts.find(uAddress);
				return itCount != g_stSilent.mapIdleCounts.end() ? itCount->second : 0ull;
			};
			if (g_stSilent.bCalibrated && !g_stSilent.bFromSignature && t_bInAutoFire && fnIdleCount(uReturnAddress) < kAutoFireIdleLimit && aAutoFire.size() < 8 && std::ranges::find(aAutoFire, uReturnAddress) == aAutoFire.end())
			{
				aAutoFire.push_back(uReturnAddress);
				LogInfo("silent aim learned fire path {:#x}", uReturnAddress);
			}
			if (!g_stSilent.bFromSignature && std::erase_if(aAutoFire, [&](std::uintptr_t uAddress) { return fnIdleCount(uAddress) >= kAutoFireIdleLimit; }) > 0)
				LogInfo("silent aim dropped a fire path that is also used outside firing");
			const bool bFirePath = std::ranges::find(aAutoFire, uReturnAddress) != aAutoFire.end();
			if (!g_stSilent.bOverride)
				return;
			if (!bFirePath)
				return;
			g_stSilent.vecOriginalLocation = *pLocation;
			g_stSilent.rotOriginalRotation = *pRotation;
			if (g_stSilent.bMagicBullet)
			{
				*pLocation = g_stSilent.vecLocation;
				*pRotation = g_stSilent.rotRotation;
			}
			else
			{
				SDK::APlayerCameraManager* pCamera = Memory::IsValidObjectPtr(pController) ? pController->PlayerCameraManager : nullptr;
				*pRotation = Memory::IsValidObjectPtr(pCamera) ? LookAt(pCamera->CameraCachePrivate.POV.Location, g_stSilent.vecLocation) : g_stSilent.rotRotation;
			}
			g_stSilent.vecAppliedLocation = *pLocation;
			g_stSilent.rotAppliedRotation = *pRotation;
			g_stSilent.bLeakWatch = true;
		}

		bool SameAngle(double dFirst, double dSecond)
		{
			return std::abs(NormalizeAngle(static_cast<float>(dFirst - dSecond))) < 0.01f;
		}

		void HkLocalPlayerGetViewPoint(SDK::ULocalPlayer* pLocalPlayer, SDK::FMinimalViewInfo* pViewInfo)
		{
			Hooks::clCallGuard clGuard;
			g_clLocalPlayerGetViewPoint(pLocalPlayer, pViewInfo);
			if (!pViewInfo)
				return;
			std::scoped_lock lckState(g_stSilent.mtxState);
			if (!g_stSilent.bLeakWatch)
				return;
			const bool bShowsOverride = SameAngle(pViewInfo->Rotation.Pitch, g_stSilent.rotAppliedRotation.Pitch) && SameAngle(pViewInfo->Rotation.Yaw, g_stSilent.rotAppliedRotation.Yaw);
			const bool bDiffersFromOriginal = !SameAngle(pViewInfo->Rotation.Pitch, g_stSilent.rotOriginalRotation.Pitch) || !SameAngle(pViewInfo->Rotation.Yaw, g_stSilent.rotOriginalRotation.Yaw);
			if (!bShowsOverride || !bDiffersFromOriginal)
				return;
			pViewInfo->Location = g_stSilent.vecOriginalLocation;
			pViewInfo->Rotation = g_stSilent.rotOriginalRotation;
			static std::atomic<int> s_iRestored = 0;
			if (s_iRestored.fetch_add(1) < 5)
				LogInfo("silent aim override reached the client view and was restored");
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

		[[nodiscard]] SDK::USBZTankAttributeSet* FindTankAttributes(SDK::ASBZCharacter* pCharacter)
		{
			SDK::USBZAbilitySystemComponent* pAbilitySystem = OW_FIELD(pCharacter, AbilitySystem);
			if (!Memory::IsValidObjectPtr(pAbilitySystem))
				return nullptr;
			SDK::UAbilitySystemComponent* pBase = pAbilitySystem;
			SDK::TArray<SDK::UAttributeSet*>& aAttributes = OW_FIELD(pBase, SpawnedAttributes);
			for (int iAttribute = 0; iAttribute < aAttributes.Num(); ++iAttribute)
			{
				SDK::UAttributeSet* pSet = aAttributes[iAttribute];
				if (Memory::IsValidObjectPtr(pSet) && pSet->IsA(SDK::USBZTankAttributeSet::StaticClass()))
					return static_cast<SDK::USBZTankAttributeSet*>(pSet);
			}
			return nullptr;
		}

		[[nodiscard]] std::optional<SDK::FVector> DozerFacePlatePoint(SDK::ASBZCharacter* pCharacter)
		{
			SDK::USBZTankAttributeSet* pTank = FindTankAttributes(pCharacter);
			if (!pTank || pTank->VisorArmor.CurrentValue <= 0.f)
				return std::nullopt;
			auto* pComponent = static_cast<SDK::USBZTankAIComponent*>(pCharacter->GetComponentByClass(SDK::USBZTankAIComponent::StaticClass()));
			if (!Payday3::IsAlive(pComponent))
				return std::nullopt;
			SDK::TArray<SDK::USBZDozerFaceArmor*>& aPlates = OW_FIELD(pComponent, FaceArmor);
			for (int iPlate = 0; iPlate < aPlates.Num(); ++iPlate)
			{
				SDK::USBZDozerFaceArmor* pPlate = aPlates[iPlate];
				if (!Payday3::IsAlive(pPlate) || !pPlate->IsVisible())
					continue;
				const SDK::FBoxSphereBounds stBounds = pPlate->GetTightBounds(false);
				return stBounds.SphereRadius > 0.0 ? stBounds.Origin : pPlate->K2_GetComponentLocation();
			}
			return std::nullopt;
		}

		[[nodiscard]] bool IsLoud()
		{
			SDK::UWorld* pWorld = Unreal::GetWorld();
			if (!pWorld || !Payday3::IsAlive(pWorld->GameState) || !pWorld->GameState->IsA(SDK::APD3HeistGameState::StaticClass()))
				return true;
			return static_cast<SDK::APD3HeistGameState*>(pWorld->GameState)->CurrentHeistState >= SDK::EPD3HeistState::Alarm;
		}

		[[nodiscard]] bool IsCasingNow()
		{
			SDK::ASBZPlayerCharacter* pCharacter = Payday3::GetLocalCharacter();
			return pCharacter && pCharacter->IsCasing();
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
				if (rSpec.ActiveCount > 0)
					continue;
				t_bInAutoFire = true;
				fnTryActivate(pAbilities, static_cast<std::uint32_t>(rSpec.Handle.Handle), true);
				t_bInAutoFire = false;
			}
		}

		void SetOverride(bool bOverride, const SDK::FVector& vecLocation = {}, const SDK::FRotator& rotRotation = {}, bool bMagicBullet = true)
		{
			std::scoped_lock lckState(g_stSilent.mtxState);
			g_stSilent.bOverride = bOverride;
			if (bOverride)
			{
				g_stSilent.bMagicBullet = bMagicBullet;
				g_stSilent.vecLocation = vecLocation;
				g_stSilent.rotRotation = rotRotation;
			}
		}

		class clAimbotFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "aimbot"; }
			[[nodiscard]] std::string_view GetGroup() const override { return "aimbot"; }

			bool Setup() override
			{
				m_pEnabled = &Config::Add("aimbot.enabled", false);
				m_pVisibleCheck = &Config::Add("aimbot.visible_check", false);
				m_pDrawFov = &Config::Add("aimbot.draw_fov", false);
				m_pKey = &Config::Add("aimbot.key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				Config::SetParent("aimbot.key", "aimbot.enabled");
				m_pType = &Config::Add("aimbot.type", 0);
				m_pMagicBullet = &Config::Add("aimbot.magic_bullet", false);
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
				if (g_clFireView.IsInstalled() || g_clGetPlayerViewPoint.IsInstalled() || !Payday3::IsReady())
					return;
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				if (!pController || !pController->AcknowledgedPawn || pController == m_pHookAttempt)
					return;
				m_pHookAttempt = pController;
				ResetSilentCalibration();
				if (const std::uintptr_t uFireView = FindFireView())
				{
					m_iLearnShots = 0;
					if (g_clFireView.Install(uFireView, &HkFireView))
					{
						LogInfo("silent aim fire view hook installed (controller at +{:#x})", g_uFireViewControllerOffset);
						return;
					}
				}
				LogWarning("silent aim fire view function was not found, using the viewpoint hook instead");
				ResolveFireCallSites();
				const std::size_t uSlot = g_uGetPlayerViewPointSlot;
				if (!Memory::GetVirtualMethod(pController, uSlot))
				{
					LogError("GetPlayerViewPoint vtable slot {:#x} is empty", uSlot);
					return;
				}
				m_iLearnShots = 0;
				if (g_clGetPlayerViewPoint.InstallVirtual(pController, uSlot, &HkGetPlayerViewPoint))
					LogInfo("silent aim hook installed at vtable slot {:#x}", uSlot);
				InstallClientViewGuard();
			}

			void Destroy() override
			{
				SetOverride(false);
				Payday3::SetAimbotTarget(nullptr);
				m_iLearnShots = 0;
				g_clLocalPlayerGetViewPoint.Remove();
				g_clGetPlayerViewPoint.Remove();
				g_clFireView.Remove();
				ResetSilentCalibration();
				m_pHookAttempt = nullptr;
			}

			void Render() override
			{
				if (*m_pDrawFov && *m_pFov > 0)
					OmegaGUI::GetBackgroundDrawList().AddCircle((OmegaGUI::GetDisplaySize() * .5f).Floor(), static_cast<float>(*m_pFov), *m_pFovColor, 100, 1.f);
				m_bGuiWantsMouse = OmegaGUI::WantsMouse();
				if (m_bAimQueued.exchange(true))
					return;
				if (!Unreal::RunOnGameThread([this] {
						Features::Guard(*this, "game thread", [this] { UpdateAim(); });
						m_bAimQueued = false;
					}))
					m_bAimQueued = false;
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				OmegaGUI::BeginColumns(2);
				if (OmegaGUI::BeginSection("aimbot.section_general"_l))
				{
					OmegaGUI::Checkbox("aimbot.enabled"_l, m_pEnabled);
					if (*m_pEnabled)
					{
						OmegaGUI::Checkbox("aimbot.visible_check"_l, m_pVisibleCheck);
						OmegaGUI::Checkbox("aimbot.auto_fire"_l, m_pAutoFire);
						OmegaGUI::ItemTooltip("aimbot.auto_fire_hint"_t);
						if (*m_pAutoFire)
						{
							OmegaGUI::Indent();
							OmegaGUI::Checkbox("aimbot.auto_fire_loud_only"_l, m_pAutoFireLoudOnly);
							OmegaGUI::ItemTooltip("aimbot.auto_fire_loud_only_hint"_t);
							OmegaGUI::Unindent();
						}
						if (*m_pType == 0)
						{
							OmegaGUI::Spacing();
							OmegaGUI::TextWrapped(OmegaLocalization::Format("aimbot.silent_status", SilentStatus()));
						}
					}
					OmegaGUI::EndSection();
				}
				OmegaGUI::NextColumn();
				if (*m_pEnabled && OmegaGUI::BeginSection("aimbot.section_settings"_l))
				{
					Widgets::Keybind("aimbot.key"_l, *m_pKey);
					OmegaGUI::TextMuted("aimbot.key_hint"_t);
					const std::array<std::string_view, 2> aTypes = { "aimbot.type_silent"_t, "aimbot.type_snapping"_t };
					OmegaGUI::Combo("aimbot.type"_l, m_pType, aTypes);
					if (*m_pType == 0)
					{
						OmegaGUI::Checkbox("aimbot.magic_bullet"_l, m_pMagicBullet);
						OmegaGUI::ItemTooltip("aimbot.magic_bullet_hint"_t);
					}
					const std::array<std::string_view, 7> aBones = { "aimbot.bone_head"_t, "aimbot.bone_torso"_t, "aimbot.bone_left_arm"_t, "aimbot.bone_right_arm"_t, "aimbot.bone_hips"_t, "aimbot.bone_left_leg"_t, "aimbot.bone_right_leg"_t };
					OmegaGUI::Combo("aimbot.target"_l, m_pTarget, aBones);
					OmegaGUI::SliderInt("aimbot.fov"_l, m_pFov, 0, 200);
					OmegaGUI::ItemTooltip("aimbot.fov_hint"_t);
					if (*m_pFov > 0)
					{
						OmegaGUI::Indent();
						OmegaGUI::Checkbox("aimbot.draw_fov"_l, m_pDrawFov);
						if (*m_pDrawFov)
							OmegaGUI::ColorEdit("aimbot.fov_color"_l, m_pFovColor);
						OmegaGUI::Unindent();
					}
					if (*m_pType == 1)
						OmegaGUI::SliderInt("aimbot.smoothing"_l, m_pSmoothing, 0, 100);
					OmegaGUI::EndSection();
				}
				OmegaGUI::EndColumns();
			}

		private:
			void InstallClientViewGuard()
			{
				if (g_clLocalPlayerGetViewPoint.IsInstalled())
					return;
				const bool bQueued = Unreal::RunOnGameThread([this] {
					Features::Guard(*this, "game thread", [] {
						SDK::ULocalPlayer* pLocalPlayer = Unreal::GetLocalPlayer(0);
						SDK::APlayerController* pController = Unreal::GetPlayerController();
						SDK::APlayerCameraManager* pCamera = Memory::IsValidObjectPtr(pController) ? pController->PlayerCameraManager : nullptr;
						std::size_t uSlot = kLocalPlayerGetViewPointIndex;
						if (Memory::IsValidObjectPtr(pLocalPlayer))
						{
							const std::uintptr_t uFunction = FindLocalPlayerGetViewPoint();
							for (std::size_t uCandidate = 0x20; uFunction && uCandidate < 0x90; ++uCandidate)
							{
								if (reinterpret_cast<std::uintptr_t>(Memory::GetVirtualMethod(pLocalPlayer, uCandidate)) != uFunction)
									continue;
								uSlot = uCandidate;
								LogInfo("ULocalPlayer::GetViewPoint found by signature in vtable slot {:#x}", uSlot);
								break;
							}
						}
						const auto pfnGetViewPoint = reinterpret_cast<FnLocalPlayerGetViewPoint>(Memory::IsValidObjectPtr(pLocalPlayer) ? Memory::GetVirtualMethod(pLocalPlayer, uSlot) : nullptr);
						if (!pfnGetViewPoint || !Memory::IsValidObjectPtr(pCamera))
							return;
						SDK::FMinimalViewInfo stView{};
						OmegaRenderHook::stFault stFault;
						auto fnProbe = [&] { pfnGetViewPoint(pLocalPlayer, &stView); };
						if (!OmegaRenderHook::InvokeGuarded(fnProbe, stFault))
						{
							LogWarning("ULocalPlayer::GetViewPoint check faulted at slot {:#x}, the client view guard is off", uSlot);
							return;
						}
						const SDK::FVector& vecCamera = pCamera->CameraCachePrivate.POV.Location;
						const bool bMatches = std::abs(stView.Location.X - vecCamera.X) < 100.0 && std::abs(stView.Location.Y - vecCamera.Y) < 100.0 && std::abs(stView.Location.Z - vecCamera.Z) < 100.0 && stView.FOV > 1.f && stView.FOV < 180.f;
						if (!bMatches)
						{
							LogWarning("vtable slot {:#x} of the local player does not return the camera view, the client view guard is off", uSlot);
							return;
						}
						if (g_clLocalPlayerGetViewPoint.InstallVirtual(pLocalPlayer, uSlot, &HkLocalPlayerGetViewPoint))
							LogInfo("client view guard installed at local player vtable slot {:#x}", uSlot);
					});
				});
				if (!bQueued)
					LogWarning("could not queue the client view guard install");
			}

			[[nodiscard]] static std::string_view SilentStatus()
			{
				using namespace OmegaLocalization::Literals;
				if (g_clFireView.IsInstalled())
					return "aimbot.silent_ready"_t;
				if (!g_clGetPlayerViewPoint.IsInstalled())
					return "aimbot.silent_no_hook"_t;
				std::scoped_lock lckState(g_stSilent.mtxState);
				return !g_stSilent.aAutoFireReturns.empty() ? "aimbot.silent_ready"_t : "aimbot.silent_learning"_t;
			}

			void UpdateAim()
			{
				Payday3::SetAimbotTarget(nullptr);
				const bool bKeyHeld = m_pKey->uKey == 0 || m_pKey->bActive;
				if (!*m_pEnabled || !bKeyHeld || !Payday3::IsReady())
				{
					m_pLockedTarget = nullptr;
					SetOverride(false);
					return;
				}

				if (IsCasingNow() || (*m_pAutoFire && *m_pAutoFireLoudOnly && !IsLoud()))
				{
					m_pLockedTarget = nullptr;
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

				const SDK::FVector vecCamera = pCamera->CameraCachePrivate.POV.Location;
				const SDK::FRotator rotCamera = pCamera->CameraCachePrivate.POV.Rotation;
				const double dYaw = rotCamera.Yaw * (std::numbers::pi / 180.0);
				const double dPitch = rotCamera.Pitch * (std::numbers::pi / 180.0);
				const SDK::FVector vecForward(std::cos(dPitch) * std::cos(dYaw), std::cos(dPitch) * std::sin(dYaw), std::sin(dPitch));
				const int iBone = std::clamp(*m_pTarget, 0, static_cast<int>(g_aBones.size()) - 1);

				const std::optional<stTarget> optTarget = SelectTarget(pController, vecCamera, vecForward, static_cast<float>(std::max(*m_pFov, 0)), g_aBones[iBone]);
				if (!optTarget)
				{
					m_pLockedTarget = nullptr;
					SetOverride(false);
					return;
				}
				m_pLockedTarget = optTarget->pCharacter;
				Payday3::SetAimbotTarget(optTarget->pCharacter);

				const SDK::FRotator rotDesired = SDK::UKismetMathLibrary::FindLookAtRotation(vecCamera, optTarget->vecAimWorld);
				const bool bAutoFire = *m_pAutoFire && (!*m_pAutoFireLoudOnly || IsLoud());
				if (bAutoFire)
					QueueAutoFire();
				if (*m_pType == 0)
				{
					const bool bManualFire = !m_bGuiWantsMouse && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
					if (bManualFire && !bAutoFire && LearningNeeded() && m_iLearnShots < kMaxLearnShots && std::chrono::steady_clock::now() >= m_tpNextLearn)
					{
						++m_iLearnShots;
						m_tpNextLearn = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
						QueueAutoFire(true);
					}
					SetOverride(bAutoFire || bManualFire, optTarget->vecAimWorld, rotDesired, *m_pMagicBullet);
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
						Features::Guard(*this, "game thread", [flYaw, flPitch] {
							SDK::APlayerController* pGameController = Payday3::IsReady() ? Unreal::GetPlayerController() : nullptr;
							if (Memory::IsValidObjectPtr(pGameController) && Payday3::IsAlive(pGameController) && Memory::IsValidObjectPtr(pGameController->AcknowledgedPawn) && Payday3::IsAlive(pGameController->AcknowledgedPawn)
								&& Memory::IsValidObjectPtr(pGameController->PlayerCameraManager))
							{
								pGameController->AddYawInput(flYaw);
								pGameController->AddPitchInput(flPitch);
							}
						});
					}))
					m_bInputQueued = false;
			}

			[[nodiscard]] static bool LearningNeeded()
			{
				std::scoped_lock lckState(g_stSilent.mtxState);
				return g_stSilent.bCalibrated && !g_stSilent.bFromSignature && g_stSilent.aAutoFireReturns.size() < kWantedFirePaths;
			}

			void QueueAutoFire(bool bLearnOnly = false)
			{
				if (m_bFireQueued.exchange(true))
					return;
				if (!Unreal::RunOnGameThread([this, bLearnOnly] {
						m_bFireQueued = false;
						Features::Guard(*this, "game thread", [this, bLearnOnly] {
							if (bLearnOnly || *m_pAutoFire)
								ActivateFireAbility();
						});
					}))
					m_bFireQueued = false;
			}

			[[nodiscard]] static int PriorityTier(Payday3::EEnemyType eType)
			{
				if (eType == Payday3::EEnemyType::Cloaker || eType == Payday3::EEnemyType::Taser)
					return 0;
				return eType == Payday3::EEnemyType::Dozer ? 1 : 2;
			}

			struct stRankedTarget
			{
				stTarget stTarget;
				int iTier = 0;
				int iDistanceBucket = 0;
			};

			std::optional<stTarget> SelectTarget(SDK::APlayerController* pController, const SDK::FVector& vecCamera, const SDK::FVector& vecForward, float flFov, const wchar_t* wszBone) const
			{
				SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns();
				if (!pPawns)
					return std::nullopt;

				const bool bFov = flFov > 0.f;
				int iWidth = 0;
				int iHeight = 0;
				if (bFov)
				{
					pController->GetViewportSize(&iWidth, &iHeight);
					if (iWidth <= 0 || iHeight <= 0)
						return std::nullopt;
				}

				const float flCenterX = static_cast<float>(iWidth) * .5f;
				const float flCenterY = static_cast<float>(iHeight) * .5f;
				const SDK::FName nameBone = Payday3::CachedLiteralName(wszBone);
				const SDK::FName nameHead = Payday3::CachedLiteralName(g_aBones[0]);
				SDK::APawn* pLocalPawn = pController->AcknowledgedPawn;

				std::optional<stRankedTarget> optBest;
				std::optional<stRankedTarget> optLocked;
				const auto fnBetter = [](const stRankedTarget& rFirst, const stRankedTarget& rSecond) {
					if (rFirst.iTier != rSecond.iTier)
						return rFirst.iTier < rSecond.iTier;
					if (rFirst.iDistanceBucket != rSecond.iDistanceBucket)
						return rFirst.iDistanceBucket < rSecond.iDistanceBucket;
					return rFirst.stTarget.flScore < rSecond.stTarget.flScore;
				};
				SDK::TArray<SDK::UObject*>& aActors = pPawns->Objects;
				for (int iActor = 0; iActor < aActors.Num(); ++iActor)
				{
					auto* pActor = static_cast<SDK::AActor*>(aActors[iActor]);
					if (!Memory::IsValidObjectPtr(pActor) || pActor == pLocalPawn)
						continue;
					SDK::USkeletalMeshComponent* pMesh = Payday3::GetPawnMesh(pActor);
					if (!pMesh || !Payday3::IsPawnAlive(pActor))
						continue;

					const Payday3::EEnemyType eType = Payday3::ResolveEnemyType(pActor);
					if (Payday3::GetEnemyInfo(eType).eCategory != Payday3::EEnemyCategory::Cop)
						continue;
					if (*m_pVisibleCheck && !pController->LineOfSightTo(pActor, vecCamera, false))
						continue;

					SDK::FVector vecAim;
					if (eType == Payday3::EEnemyType::Dozer)
					{
						const std::optional<SDK::FVector> optPlate = DozerFacePlatePoint(static_cast<SDK::ASBZCharacter*>(pActor));
						vecAim = optPlate ? *optPlate : pMesh->GetSocketLocation(nameHead);
					}
					else if (eType == Payday3::EEnemyType::Drone)
						vecAim = Payday3::GetActorLocation(pActor);
					else
						vecAim = pMesh->GetSocketLocation(nameBone);
					const SDK::FVector vecOffset = vecAim - vecCamera;
					float flScore = 0.f;
					if (bFov)
					{
						SDK::FVector2D vecScreen;
						if (!pController->ProjectWorldLocationToScreen(vecAim, &vecScreen, false))
							continue;
						flScore = static_cast<float>(std::hypot(vecScreen.X - flCenterX, vecScreen.Y - flCenterY));
						if (flScore > flFov)
							continue;
					}
					else
					{
						if (vecOffset.Magnitude() < 1.0)
							continue;
						flScore = 1.f - static_cast<float>(vecOffset.GetNormalized().Dot(vecForward));
					}
					const stRankedTarget stCandidate{ stTarget{ pActor, vecAim, flScore }, PriorityTier(eType), static_cast<int>(vecOffset.Magnitude() / 100.0 / kDistanceBucketMeters) };
					if (pActor == m_pLockedTarget)
						optLocked = stCandidate;
					if (!optBest || fnBetter(stCandidate, *optBest))
						optBest = stCandidate;
				}
				if (optLocked && optBest && optLocked->iTier == optBest->iTier)
					return optLocked->stTarget;
				return optBest ? std::optional<stTarget>(optBest->stTarget) : std::nullopt;
			}

			bool* m_pEnabled = nullptr;
			bool* m_pVisibleCheck = nullptr;
			bool* m_pDrawFov = nullptr;
			Config::stKeybind* m_pKey = nullptr;
			int* m_pType = nullptr;
			bool* m_pMagicBullet = nullptr;
			int* m_pTarget = nullptr;
			OmegaGUI::color* m_pFovColor = nullptr;
			int* m_pFov = nullptr;
			int* m_pSmoothing = nullptr;
			bool* m_pAutoFire = nullptr;
			bool* m_pAutoFireLoudOnly = nullptr;
			std::atomic<bool> m_bFireQueued = false;
			int m_iLearnShots = 0;
			std::chrono::steady_clock::time_point m_tpNextLearn;
			SDK::APlayerController* m_pHookAttempt = nullptr;
			mutable SDK::AActor* m_pLockedTarget = nullptr;
			std::atomic<bool> m_bInputQueued = false;
			std::atomic<bool> m_bAimQueued = false;
			std::atomic<bool> m_bGuiWantsMouse = false;
		};

		clAimbotFeature g_clAimbotFeature;
	}
}

#endif
