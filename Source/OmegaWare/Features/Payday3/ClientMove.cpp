#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include <Windows.h>

#include "Config/Config.hpp"
#include "Core/Log.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"
#include "Menu/Widgets.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace OmegaWare
{
	namespace
	{
		SDK::USBZPlayerMovementComponent* GetMovement(SDK::ASBZPlayerCharacter* pCharacter)
		{
			if (!pCharacter)
				return nullptr;
			SDK::UCharacterMovementComponent* pMovement = pCharacter->CharacterMovement;
			if (!Memory::IsValidObjectPtr(pMovement) || !pMovement->IsA(SDK::USBZPlayerMovementComponent::StaticClass()))
				return nullptr;
			return static_cast<SDK::USBZPlayerMovementComponent*>(pMovement);
		}

		class clClientMoveFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "clientmove"; }
			[[nodiscard]] std::string_view GetGroup() const override { return "player"; }

			bool Setup() override
			{
				m_pEnabled = &Config::Add("clientmove.enabled", false);
				m_pKey = &Config::Add("clientmove.key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				m_pFasterKey = &Config::Add("clientmove.faster_key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				m_pSyncKey = &Config::Add("clientmove.sync_key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				for (const char* szKey : { "clientmove.key", "clientmove.faster_key", "clientmove.sync_key" })
					Config::SetParent(szKey, "clientmove.enabled");
				m_pAutoSync = &Config::Add("clientmove.auto_sync", true);
				m_pSpeed = &Config::Add("clientmove.speed", 1200.f);
				m_pFreecam = &Config::Add("freecam.enabled", false);
				m_pFreecamKey = &Config::Add("freecam.key", Config::stKeybind{ .eMode = Config::EKeybindMode::Toggle });
				m_pFreecamFasterKey = &Config::Add("freecam.faster_key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				m_pFreecamSpeed = &Config::Add("freecam.speed", 800.f);
				for (const char* szKey : { "freecam.key", "freecam.faster_key" })
					Config::SetParent(szKey, "freecam.enabled");
				return true;
			}

			void Render() override
			{
				const bool bSyncDown = m_pSyncKey->bActive;
				if (bSyncDown && !m_bSyncWasDown)
					m_bSyncRequested = true;
				m_bSyncWasDown = bSyncDown;
				UpdateFreecam();
			}

			void Run() override
			{
				if (!Payday3::IsReady() || m_bQueued.exchange(true))
					return;
				if (!Unreal::RunOnGameThread([this] {
						m_bQueued = false;
						Features::Guard(*this, "game thread", [this] { RunGame(); });
					}))
					m_bQueued = false;
			}

			void RunGame()
			{
				const bool bSyncRequested = m_bSyncRequested.exchange(false);
				SDK::ASBZPlayerCharacter* pCharacter = Payday3::GetLocalCharacter();
				SDK::USBZPlayerMovementComponent* pMovement = GetMovement(pCharacter);
				if (!pMovement)
				{
					m_bActive = false;
					return;
				}

				if (*m_pEnabled && m_pKey->uKey != 0 && m_pKey->bActive)
				{
					EnableNoclip(pCharacter, pMovement);
					m_bActive = true;
					if (bSyncRequested)
						SyncPosition(pCharacter, pMovement);
				}
				else if (m_bActive)
				{
					DisableNoclip(pCharacter, pMovement);
					SDK::UWorld* pWorld = Unreal::GetWorld();
					if (*m_pAutoSync && pWorld && !SDK::UKismetSystemLibrary::IsServer(pWorld))
						SyncPosition(pCharacter, pMovement);
					m_bActive = false;
				}
			}

			void Destroy() override
			{
				if ((!m_bActive && !m_bFreecamLive) || !Payday3::IsReady())
					return;
				m_bFreecamWanted = false;
				Unreal::RunOnGameThreadAndWait([this] {
					if (m_pFreecamActor)
						EndFreecam();
					if (!m_bActive)
						return;
					SDK::ASBZPlayerCharacter* pCharacter = Payday3::GetLocalCharacter();
					if (SDK::USBZPlayerMovementComponent* pMovement = GetMovement(pCharacter))
						DisableNoclip(pCharacter, pMovement);
				}, std::chrono::seconds(1));
				m_bActive = false;
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				OmegaGUI::BeginColumns(2);
				if (OmegaGUI::BeginSection("clientmove.section_noclip"_l))
				{
					OmegaGUI::Checkbox("clientmove.enabled"_l, m_pEnabled);
					if (*m_pEnabled)
					{
						OmegaGUI::Checkbox("clientmove.auto_sync"_l, m_pAutoSync);
						OmegaGUI::SliderFloat("clientmove.speed"_l, m_pSpeed, 100.f, 5000.f, "{:.0f}");
						Widgets::Keybind("clientmove.key"_l, *m_pKey);
						Widgets::Keybind("clientmove.faster_key"_l, *m_pFasterKey);
						Widgets::Keybind("clientmove.sync_key"_l, *m_pSyncKey);
					}
					OmegaGUI::EndSection();
				}
				OmegaGUI::NextColumn();
				if (OmegaGUI::BeginSection("clientmove.section_freecam"_l))
				{
					OmegaGUI::Checkbox("clientmove.freecam"_l, m_pFreecam);
					OmegaGUI::ItemTooltip("clientmove.freecam_hint"_t);
					if (*m_pFreecam)
					{
						OmegaGUI::SliderFloat("clientmove.freecam_speed"_l, m_pFreecamSpeed, 100.f, 5000.f, "{:.0f}");
						Widgets::Keybind("clientmove.freecam_key"_l, *m_pFreecamKey);
						Widgets::Keybind("clientmove.freecam_faster_key"_l, *m_pFreecamFasterKey);
					}
					OmegaGUI::EndSection();
				}
				OmegaGUI::EndColumns();
			}

		private:
			void UpdateFreecam()
			{
				const bool bWanted = *m_pFreecam && m_pFreecamKey->uKey != 0 && m_pFreecamKey->bActive;
				m_bFreecamWanted = bWanted;
				if (!bWanted && !m_bFreecamLive)
					return;
				if (bWanted)
				{
					const bool bKeys = !OmegaGUI::WantsKeyboard();
					auto fnDown = [bKeys](int iKey) { return bKeys && (GetAsyncKeyState(iKey) & 0x8000) != 0; };
					m_flFreecamForward = static_cast<float>(fnDown('W')) - static_cast<float>(fnDown('S'));
					m_flFreecamRight = static_cast<float>(fnDown('D')) - static_cast<float>(fnDown('A'));
					m_flFreecamUp = static_cast<float>(fnDown(VK_SPACE)) - static_cast<float>(fnDown(VK_LCONTROL));
					m_bFreecamFast = m_pFreecamFasterKey->uKey != 0 && m_pFreecamFasterKey->bActive;
					m_flFreecamDelta = m_flFreecamDelta + OmegaGUI::GetDeltaTime();
				}
				if (!Payday3::IsReady() || m_bFreecamQueued.exchange(true))
					return;
				if (!Unreal::RunOnGameThread([this] {
						m_bFreecamQueued = false;
						Features::Guard(*this, "game thread", [this] { StepFreecam(); });
					}))
					m_bFreecamQueued = false;
			}

			void StepFreecam()
			{
				const bool bWanted = m_bFreecamWanted;
				const float flDelta = std::clamp(m_flFreecamDelta.exchange(0.f), 0.f, .1f);
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				if (m_pFreecamActor && (!Payday3::IsAliveActor(m_pFreecamActor) || pController != m_pFreecamController))
					AbandonFreecam();
				if (!bWanted)
				{
					if (m_pFreecamActor)
						EndFreecam();
					return;
				}
				if (!pController || !Payday3::IsAlive(pController))
					return;
				if (!m_pFreecamActor && !BeginFreecam(pController))
					return;

				const SDK::FRotator rotView = pController->GetControlRotation();
				const double dYaw = rotView.Yaw * (std::numbers::pi / 180.0);
				const double dPitch = rotView.Pitch * (std::numbers::pi / 180.0);
				const SDK::FVector vecForward(std::cos(dPitch) * std::cos(dYaw), std::cos(dPitch) * std::sin(dYaw), std::sin(dPitch));
				const SDK::FVector vecRight(-std::sin(dYaw), std::cos(dYaw), 0.0);
				SDK::FVector vecMove = vecForward * static_cast<double>(m_flFreecamForward.load()) + vecRight * static_cast<double>(m_flFreecamRight.load()) + SDK::FVector(0.0, 0.0, static_cast<double>(m_flFreecamUp.load()));
				if (vecMove.Magnitude() > 1.0)
					vecMove = vecMove.GetNormalized();
				const double dSpeed = static_cast<double>(*m_pFreecamSpeed) * (m_bFreecamFast ? 3.0 : 1.0);
				m_vecFreecamLocation = m_vecFreecamLocation + vecMove * (dSpeed * flDelta);
				m_pFreecamActor->K2_SetActorLocationAndRotation(m_vecFreecamLocation, rotView, false, nullptr, true);
			}

			bool BeginFreecam(SDK::APlayerController* pController)
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::APlayerCameraManager* pCamera = pController->PlayerCameraManager;
				if (!pWorld || !Memory::IsValidObjectPtr(pCamera))
					return false;
				const SDK::FVector vecStart = pCamera->GetCameraLocation();
				const SDK::FRotator rotStart = pCamera->GetCameraRotation();
				SDK::FTransform stTransform{};
				stTransform.Rotation.W = 1.0;
				stTransform.Translation = vecStart;
				stTransform.Scale3D = SDK::FVector(1.0, 1.0, 1.0);
				SDK::AActor* pSpawned = SDK::UGameplayStatics::BeginDeferredActorSpawnFromClass(pWorld, SDK::ACameraActor::StaticClass(), stTransform, SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr, SDK::ESpawnActorScaleMethod::OverrideRootScale);
				if (!Payday3::IsAliveActor(pSpawned))
				{
					LogError("freecam: could not spawn a camera actor");
					return false;
				}
				SDK::UGameplayStatics::FinishSpawningActor(pSpawned, stTransform, SDK::ESpawnActorScaleMethod::OverrideRootScale);
				pSpawned->K2_SetActorLocationAndRotation(vecStart, rotStart, false, nullptr, true);
				pController->SetViewTargetWithBlend(pSpawned, 0.f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.f, false);
				pController->SetIgnoreMoveInput(true);
				m_pFreecamActor = static_cast<SDK::ACameraActor*>(pSpawned);
				m_pFreecamController = pController;
				m_vecFreecamLocation = vecStart;
				m_bFreecamIgnoring = true;
				m_bFreecamLive = true;
				LogInfo("freecam: started");
				return true;
			}

			void EndFreecam()
			{
				SDK::APlayerController* pController = m_pFreecamController;
				if (Payday3::IsAlive(pController))
				{
					SDK::APawn* pPawn = pController->AcknowledgedPawn;
					if (Payday3::IsAliveActor(pPawn))
						pController->SetViewTargetWithBlend(pPawn, 0.f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.f, false);
					else
						pController->SetViewTargetWithBlend(pController, 0.f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.f, false);
					if (m_bFreecamIgnoring)
						pController->SetIgnoreMoveInput(false);
				}
				if (Payday3::IsAliveActor(m_pFreecamActor))
					m_pFreecamActor->K2_DestroyActor();
				AbandonFreecam();
				LogInfo("freecam: stopped");
			}

			void AbandonFreecam()
			{
				m_pFreecamActor = nullptr;
				m_pFreecamController = nullptr;
				m_bFreecamIgnoring = false;
				m_bFreecamLive = false;
			}

			void EnableNoclip(SDK::ASBZPlayerCharacter* pCharacter, SDK::USBZPlayerMovementComponent* pMovement) const
			{
				if (pCharacter->GetActorEnableCollision())
					pCharacter->SetActorEnableCollision(false);

				pMovement->MovementMode = SDK::EMovementMode::MOVE_Flying;
				pMovement->BrakingDecelerationFlying = 10000.f;
				pMovement->MaxFlySpeed = 10000.f;

				float flSpeed = *m_pSpeed;
				if (m_pFasterKey->uKey != 0 && m_pFasterKey->bActive)
					flSpeed *= 2.f;
				pMovement->Velocity = pMovement->GetLastInputVector() * flSpeed;
			}

			static void DisableNoclip(SDK::ASBZPlayerCharacter* pCharacter, SDK::USBZPlayerMovementComponent* pMovement)
			{
				pCharacter->SetActorEnableCollision(true);
				pMovement->MovementMode = SDK::EMovementMode::MOVE_Walking;
				pMovement->Velocity = SDK::FVector{};
			}

			static void SyncPosition(SDK::ASBZPlayerCharacter* pCharacter, SDK::USBZPlayerMovementComponent* pMovement)
			{
				const SDK::FVector vecPoint = pCharacter->K2_GetActorLocation();
				SDK::FSBZMinimalAgilityTraversalTrajectory stTrajectory{};
				stTrajectory.JumpPointLocation = vecPoint;
				stTrajectory.EdgePointFrontLocation = vecPoint;
				stTrajectory.EdgePointBackLocation = vecPoint;
				stTrajectory.LandPointLocation = vecPoint;
				stTrajectory.EntrySpeed = (std::numeric_limits<std::int16_t>::max)();
				stTrajectory.AgilityType = SDK::ESBZAgilityTraversalType::VaultLowFast;
				stTrajectory.bEndsInCrouchState = false;
				stTrajectory.bEndsFalling = false;
				pMovement->Server_StartTraversal(stTrajectory);
			}

			bool* m_pEnabled = nullptr;
			Config::stKeybind* m_pKey = nullptr;
			Config::stKeybind* m_pFasterKey = nullptr;
			Config::stKeybind* m_pSyncKey = nullptr;
			bool* m_pAutoSync = nullptr;
			float* m_pSpeed = nullptr;

			bool* m_pFreecam = nullptr;
			Config::stKeybind* m_pFreecamKey = nullptr;
			Config::stKeybind* m_pFreecamFasterKey = nullptr;
			float* m_pFreecamSpeed = nullptr;
			std::atomic<bool> m_bFreecamWanted = false;
			std::atomic<bool> m_bFreecamLive = false;
			std::atomic<bool> m_bFreecamQueued = false;
			std::atomic<bool> m_bFreecamFast = false;
			std::atomic<float> m_flFreecamDelta = 0.f;
			std::atomic<float> m_flFreecamForward = 0.f;
			std::atomic<float> m_flFreecamRight = 0.f;
			std::atomic<float> m_flFreecamUp = 0.f;
			SDK::ACameraActor* m_pFreecamActor = nullptr;
			SDK::APlayerController* m_pFreecamController = nullptr;
			SDK::FVector m_vecFreecamLocation;
			bool m_bFreecamIgnoring = false;

			bool m_bActive = false;
			bool m_bSyncWasDown = false;
			std::atomic<bool> m_bSyncRequested = false;
			std::atomic<bool> m_bQueued = false;
		};

		clClientMoveFeature g_clClientMoveFeature;
	}
}

#endif
