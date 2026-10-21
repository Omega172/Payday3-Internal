#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "Config/Config.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"
#include "Menu/Widgets.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>

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

			bool Setup() override
			{
				m_pEnabled = &Config::Add("clientmove.enabled", false);
				m_pKey = &Config::Add("clientmove.key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				m_pFasterKey = &Config::Add("clientmove.faster_key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				m_pSyncKey = &Config::Add("clientmove.sync_key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				m_pAutoSync = &Config::Add("clientmove.auto_sync", false);
				m_pSpeed = &Config::Add("clientmove.speed", 1200.f);
				return true;
			}

			void Render() override
			{
				const bool bSyncDown = m_pSyncKey->bActive;
				if (bSyncDown && !m_bSyncWasDown)
					m_bSyncRequested = true;
				m_bSyncWasDown = bSyncDown;
			}

			void Run() override
			{
				if (!Payday3::IsReady() || m_bQueued.exchange(true))
					return;
				if (!Unreal::RunOnGameThread([this] {
						m_bQueued = false;
						if (IsActive())
							RunGame();
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
					if (*m_pAutoSync)
						SyncPosition(pCharacter, pMovement);
					m_bActive = false;
				}
			}

			void Destroy() override
			{
				if (!m_bActive || !Payday3::IsReady())
					return;
				Unreal::RunOnGameThreadAndWait([this] {
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
				if (OmegaGUI::BeginSection("clientmove.section_general"_l))
				{
					OmegaGUI::Checkbox("clientmove.enabled"_l, m_pEnabled);
					OmegaGUI::Checkbox("clientmove.auto_sync"_l, m_pAutoSync);
					OmegaGUI::SliderFloat("clientmove.speed"_l, m_pSpeed, 100.f, 5000.f, "{:.0f}");
					OmegaGUI::EndSection();
				}
				OmegaGUI::NextColumn();
				if (OmegaGUI::BeginSection("clientmove.section_keys"_l))
				{
					Widgets::Keybind("clientmove.key"_l, "clientmove.key", *m_pKey);
					Widgets::Keybind("clientmove.faster_key"_l, "clientmove.faster_key", *m_pFasterKey);
					Widgets::Keybind("clientmove.sync_key"_l, "clientmove.sync_key", *m_pSyncKey);
					OmegaGUI::EndSection();
				}
				OmegaGUI::EndColumns();
			}

		private:
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

			bool m_bActive = false;
			bool m_bSyncWasDown = false;
			std::atomic<bool> m_bSyncRequested = false;
			std::atomic<bool> m_bQueued = false;
		};

		clClientMoveFeature g_clClientMoveFeature;
	}
}

#endif
