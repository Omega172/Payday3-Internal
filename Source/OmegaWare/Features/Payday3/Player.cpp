#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

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
#include <array>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace OmegaWare
{
	namespace
	{
		constexpr float kInstantRate = 10000.f;

		struct stWeaponBackup
		{
			float flViewSpeedDeflect = 0.f;
			float flGunKickBackSpeedDeflect = 0.f;
			float flGunSpeedDeflect = 0.f;

			float flInnerClusterSpreadMultiplier = 0.f;
			float flFireSpreadStart = 0.f;
			float flFireSpreadMinCap = 0.f;
			float flFireSpreadCap = 0.f;
			float flFireSpreadIncrease = 0.f;

			float flRoundsPerMinute = 0.f;
			SDK::ESBZFireMode eFireMode{};
		};

		struct stAttributeBackup
		{
			float flHealthMax = 0.f;
			std::array<float, 9> aAmmo{};
		};

		struct stMontageRate
		{
			SDK::UAnimMontage* pMontage = nullptr;
			float flOriginalRate = 1.f;
			bool bApplied = false;
		};

		std::array<SDK::FGameplayAttributeData*, 9> AmmoAttributes(SDK::USBZPlayerAttributeSet* pAttributes)
		{
			return { {
				&pAttributes->PrimaryEquippableAmmoInventory,
				&pAttributes->SecondaryEquippableAmmoInventory,
				&pAttributes->TertiaryEquippableAmmoInventory,
				&pAttributes->PrimaryThrowableAmmoInventory,
				&pAttributes->SecondaryThrowableAmmoInventory,
				&pAttributes->TertiaryThrowableAmmoInventory,
				&pAttributes->PrimaryToolAmmoInventory,
				&pAttributes->SecondaryToolAmmoInventory,
				&pAttributes->TertiaryToolAmmoInventory,
			} };
		}

		SDK::USBZPlayerAttributeSet* GetAttributes(SDK::ASBZPlayerCharacter* pCharacter)
		{
			if (!pCharacter)
				return nullptr;
			SDK::USBZPlayerAttributeSet* pAttributes = pCharacter->PlayerAttributeSet;
			return Memory::IsValidObjectPtr(pAttributes) ? pAttributes : nullptr;
		}

		class clPlayerFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "player"; }

			bool Setup() override
			{
				m_pGodMode = &Config::Add("player.godmode", false);
				m_pGodModeType = &Config::Add("player.godmode_type", 0);
				m_pInfStamina = &Config::Add("player.inf_stamina", false);
				m_pInstantMelee = &Config::Add("player.instant_melee", false);
				m_pNoScreenshake = &Config::Add("player.no_screenshake", false);
				m_pNoFallDamage = &Config::Add("player.no_fall_damage", false);
				m_pNoDetection = &Config::Add("player.no_detection", false);
				m_pInstantInteraction = &Config::Add("player.instant_interaction", false);
				m_pInstantBagGrab = &Config::Add("player.instant_bag_grab", false);
				m_pInstantMinigame = &Config::Add("player.instant_minigame", false);
				m_pCustomName = &Config::Add("player.custom_name", std::string());
				m_pAutoSecure = &Config::Add("player.auto_secure_bags", false);
				m_pReach = &Config::Add("player.reach", false);
				m_pReachKey = &Config::Add("player.reach_key", Config::stKeybind{ .eMode = Config::EKeybindMode::Hold });
				m_pTeamGodMode = &Config::Add("player.team_godmode", false);
				m_pTeamInfAmmo = &Config::Add("player.team_inf_ammo", false);
				m_pTeamInstantInteraction = &Config::Add("player.team_instant_interaction", false);
				m_pInstantReload = &Config::Add("player.instant_reload", false);
				m_pInfAmmo = &Config::Add("player.inf_ammo", false);
				m_pNoRecoil = &Config::Add("player.no_recoil", false);
				m_pNoSpread = &Config::Add("player.no_spread", false);
				m_pFireRate = &Config::Add("player.fire_rate", false);
				m_pFireRateMultiplier = &Config::Add("player.fire_rate_multiplier", 100);
				return true;
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

			void Destroy() override
			{
				if (Payday3::IsReady())
					Unreal::RunOnGameThreadAndWait([this] { RestoreAll(); }, std::chrono::seconds(1));
			}

			void RunGame()
			{
				if (!m_bWeaponsBackedUp)
					m_bWeaponsBackedUp = BackupWeaponData();

				SDK::ASBZPlayerCharacter* pCharacter = Payday3::GetLocalCharacter();
				if (pCharacter)
					BackupAttributes(pCharacter);

				const bool bGodMode = *m_pGodMode;
				const int iGodModeType = *m_pGodModeType;
				const bool bInfAmmo = *m_pInfAmmo;

				if (m_bPrevGodMode && (!bGodMode || iGodModeType != m_iPrevGodModeType))
				{
					if (m_iPrevGodModeType == 0)
						RestoreHealth(pCharacter);
					else
						BlockDamage(pCharacter, false);
				}
				if (m_bPrevInfAmmo && !bInfAmmo)
					RestoreAmmo(pCharacter);

				m_bPrevGodMode = bGodMode;
				m_iPrevGodModeType = iGodModeType;
				m_bPrevInfAmmo = bInfAmmo;

				SDK::USBZPlayerAttributeSet* pAttributes = GetAttributes(pCharacter);
				if (bGodMode && pCharacter)
				{
					if (iGodModeType == 0)
					{
						if (pAttributes)
							pAttributes->Health.CurrentValue = pAttributes->HealthMax.CurrentValue * 10.f;
					}
					else
						BlockDamage(pCharacter, true);
				}

				if (*m_pInfStamina && pAttributes)
					pAttributes->Stamina.CurrentValue = 100.f;

				ApplyMontageRate(m_stMelee, pCharacter ? pCharacter->CurrentMeleeMontage : nullptr, *m_pInstantMelee);

				if (*m_pNoScreenshake)
					if (SDK::APlayerCameraManager* pCamera = Unreal::GetCameraManager())
						pCamera->StopAllCameraShakes(true);

				if (*m_pNoFallDamage && pCharacter)
					pCharacter->FallingStartHeight = pCharacter->K2_GetActorLocation().Z;

				if (*m_pNoDetection && pCharacter)
					ClearDetection(pCharacter);

				if (pCharacter && (*m_pInstantInteraction || *m_pInstantBagGrab))
					CompleteInteraction(pCharacter);

				if (pCharacter && *m_pInstantMinigame)
					CompleteMinigame(pCharacter);

				if (pCharacter && m_bNameRequested.exchange(false))
					ApplyName(pCharacter);

				if (*m_pAutoSecure)
					SecureThrownBags();

				RunTeammates(pCharacter);

				if (pCharacter && *m_pReach && m_pReachKey->uKey != 0 && m_pReachKey->bActive)
					GrabThroughWalls(pCharacter);

				ApplyMontageRate(m_stReload, pCharacter ? pCharacter->LastLocalReloadMontage : nullptr, *m_pInstantReload);

				if (bInfAmmo && pAttributes)
					for (SDK::FGameplayAttributeData* pAmmo : AmmoAttributes(pAttributes))
						pAmmo->CurrentValue = 999.f;

				if (pCharacter && m_bWeaponsBackedUp)
				{
					const std::vector<SDK::USBZRangedWeaponData*> aWeapons = CurrentWeapons(pCharacter);
					ApplyRecoil(aWeapons, *m_pNoRecoil);
					ApplySpread(aWeapons, *m_pNoSpread);
					ApplyFireRate(aWeapons, *m_pFireRate);
				}
			}

			void RestoreAll()
			{
				if (!m_setProtected.empty())
				{
					for (SDK::ASBZPlayerCharacter* pTeammate : Teammates(Payday3::GetLocalCharacter()))
						if (m_setProtected.contains(pTeammate))
							pTeammate->bCanBeDamaged = true;
					m_setProtected.clear();
				}
				SDK::ASBZPlayerCharacter* pCharacter = Payday3::GetLocalCharacter();
				if (m_bPrevGodMode)
				{
					if (m_iPrevGodModeType == 0)
						RestoreHealth(pCharacter);
					else
						BlockDamage(pCharacter, false);
				}
				if (m_bPrevInfAmmo)
					RestoreAmmo(pCharacter);
				ApplyMontageRate(m_stMelee, pCharacter ? pCharacter->CurrentMeleeMontage : nullptr, false);
				ApplyMontageRate(m_stReload, pCharacter ? pCharacter->LastLocalReloadMontage : nullptr, false);
				if (pCharacter && m_bWeaponsBackedUp)
				{
					const std::vector<SDK::USBZRangedWeaponData*> aWeapons = CurrentWeapons(pCharacter);
					ApplyRecoil(aWeapons, false);
					ApplySpread(aWeapons, false);
					ApplyFireRate(aWeapons, false);
				}
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				if (!OmegaGUI::BeginTabBar("player.tabs"))
					return;
				if (OmegaGUI::BeginTabItem("player.tab_local"_l))
				{
					DrawLocalTab();
					OmegaGUI::EndTabItem();
				}
				if (OmegaGUI::BeginTabItem("player.tab_players"_l))
				{
					DrawPlayersTab();
					OmegaGUI::EndTabItem();
				}
				if (OmegaGUI::BeginTabItem("player.tab_weapons"_l))
				{
					DrawWeaponsTab();
					OmegaGUI::EndTabItem();
				}
				OmegaGUI::EndTabBar();
			}

		private:
			void DrawLocalTab()
			{
				using namespace OmegaLocalization::Literals;
				OmegaGUI::BeginColumns(2);
				if (OmegaGUI::BeginSection("player.section_general"_l))
				{
					OmegaGUI::Checkbox("player.godmode"_l, m_pGodMode);
					OmegaGUI::Checkbox("player.inf_stamina"_l, m_pInfStamina);
					OmegaGUI::Checkbox("player.instant_melee"_l, m_pInstantMelee);
					OmegaGUI::Checkbox("player.no_screenshake"_l, m_pNoScreenshake);
					OmegaGUI::Checkbox("player.no_fall_damage"_l, m_pNoFallDamage);
					OmegaGUI::Checkbox("player.no_detection"_l, m_pNoDetection);
					OmegaGUI::Checkbox("player.instant_interaction"_l, m_pInstantInteraction);
					OmegaGUI::Checkbox("player.instant_bag_grab"_l, m_pInstantBagGrab);
					OmegaGUI::Checkbox("player.instant_minigame"_l, m_pInstantMinigame);
					OmegaGUI::Checkbox("player.auto_secure_bags"_l, m_pAutoSecure);
					OmegaGUI::ItemTooltip("player.auto_secure_bags_hint"_t);
					OmegaGUI::Checkbox("player.reach"_l, m_pReach);
					OmegaGUI::ItemTooltip("player.reach_hint"_t);
					OmegaGUI::EndSection();
				}
				OmegaGUI::NextColumn();
				if (OmegaGUI::BeginSection("player.section_settings"_l))
				{
					const std::array<std::string_view, 2> aTypes = { "player.godmode_set"_t, "player.godmode_block"_t };
					OmegaGUI::Combo("player.godmode_type"_l, m_pGodModeType, aTypes);
					if (*m_pReach)
						Widgets::Keybind("player.reach_key"_l, "player.reach_key", *m_pReachKey);
					OmegaGUI::EndSection();
				}
				if (OmegaGUI::BeginSection("player.section_name"_l))
				{
					OmegaGUI::InputText("player.custom_name"_l, m_pCustomName, "player.name_hint"_t);
					if (OmegaGUI::Button("player.name_apply"_l))
					{
						m_bResetName = false;
						m_bNameRequested = true;
					}
					OmegaGUI::SameLine();
					if (OmegaGUI::Button("player.name_reset"_l))
					{
						m_bResetName = true;
						m_bNameRequested = true;
					}
					OmegaGUI::TextWrapped("player.name_note"_t);
					OmegaGUI::EndSection();
				}
				OmegaGUI::EndColumns();
			}

			void DrawPlayersTab()
			{
				using namespace OmegaLocalization::Literals;
				SDK::AGameStateBase* pGameState = Payday3::IsReady() ? Unreal::GetGameState() : nullptr;
				if (!pGameState)
				{
					OmegaGUI::TextMuted("player.no_players"_t);
					return;
				}

				const bool bHost = m_bIsHost;
				if (OmegaGUI::BeginSection("player.section_teammates"_l))
				{
					if (!bHost)
						OmegaGUI::TextMuted("player.teammates_host_only"_t);
					OmegaGUI::BeginDisabled(!bHost);
					OmegaGUI::Checkbox("player.team_godmode"_l, m_pTeamGodMode);
					OmegaGUI::Checkbox("player.team_inf_ammo"_l, m_pTeamInfAmmo);
					OmegaGUI::Checkbox("player.team_instant_interaction"_l, m_pTeamInstantInteraction);
					if (OmegaGUI::Button("player.bring_all"_l))
						RequestBring(nullptr);
					OmegaGUI::EndDisabled();
					OmegaGUI::EndSection();
				}

				SDK::APawn* pLocalPawn = Unreal::GetLocalPawn();
				const SDK::FVector vecOrigin = pLocalPawn ? pLocalPawn->K2_GetActorLocation() : SDK::FVector{};
				if (!OmegaGUI::BeginTable("player.table", 5))
					return;
				OmegaGUI::TableSetupColumn("player.column_name"_t);
				OmegaGUI::TableSetupColumn("player.column_health"_t);
				OmegaGUI::TableSetupColumn("player.column_distance"_t);
				OmegaGUI::TableSetupColumn("player.column_status"_t);
				OmegaGUI::TableSetupColumn("player.column_actions"_t);
				OmegaGUI::TableHeadersRow();

				SDK::TArray<SDK::APlayerState*>& aPlayers = pGameState->PlayerArray;
				for (int iPlayer = 0; iPlayer < aPlayers.Num(); ++iPlayer)
				{
					SDK::APlayerState* pState = aPlayers[iPlayer];
					if (!Memory::IsValidObjectPtr(pState))
						continue;

					SDK::APawn* pPawn = pState->PawnPrivate;
					SDK::ASBZPlayerCharacter* pCharacter = Memory::IsValidObjectPtr(pPawn) && pPawn->IsA(SDK::ASBZPlayerCharacter::StaticClass()) ? static_cast<SDK::ASBZPlayerCharacter*>(pPawn) : nullptr;
					SDK::USBZPlayerAttributeSet* pAttributes = GetAttributes(pCharacter);

					OmegaGUI::TableNextRow();
					OmegaGUI::TableNextColumn();
					OmegaGUI::TextUnformatted(pState->GetPlayerName().ToString());
					OmegaGUI::TableNextColumn();
					OmegaGUI::TextUnformatted(pAttributes ? std::format("{:.0f}", pAttributes->Health.CurrentValue) : "-");
					OmegaGUI::TableNextColumn();
					OmegaGUI::TextUnformatted(pCharacter && pLocalPawn ? std::format("{:.0f}m", (pCharacter->K2_GetActorLocation() - vecOrigin).Magnitude() / 100.f) : "-");
					OmegaGUI::TableNextColumn();
					OmegaGUI::TextUnformatted(pCharacter && pCharacter->bIsAlive ? "player.status_alive"_t : "player.status_down"_t);
					OmegaGUI::TableNextColumn();
					if (pCharacter && pPawn != pLocalPawn)
					{
						OmegaGUI::PushID(pState);
						OmegaGUI::BeginDisabled(!bHost);
						if (OmegaGUI::SmallButton("player.bring"_l))
							RequestBring(pState);
						OmegaGUI::EndDisabled();
						OmegaGUI::PopID();
					}
				}
				OmegaGUI::EndTable();
			}

			void DrawWeaponsTab()
			{
				using namespace OmegaLocalization::Literals;
				OmegaGUI::BeginColumns(2);
				if (OmegaGUI::BeginSection("player.section_general"_l))
				{
					OmegaGUI::Checkbox("player.instant_reload"_l, m_pInstantReload);
					OmegaGUI::Checkbox("player.inf_ammo"_l, m_pInfAmmo);
					OmegaGUI::Checkbox("player.no_recoil"_l, m_pNoRecoil);
					OmegaGUI::Checkbox("player.no_spread"_l, m_pNoSpread);
					OmegaGUI::Checkbox("player.fire_rate"_l, m_pFireRate);
					OmegaGUI::EndSection();
				}
				OmegaGUI::NextColumn();
				if (OmegaGUI::BeginSection("player.section_settings"_l))
				{
					OmegaGUI::SliderInt("player.fire_rate_multiplier"_l, m_pFireRateMultiplier, 0, 1000);
					OmegaGUI::EndSection();
				}
				OmegaGUI::EndColumns();
			}

			void RequestBring(SDK::APlayerState* pState)
			{
				std::scoped_lock lckBring(m_mtxBring);
				m_aBringRequests.push_back(pState);
			}

			void BlockDamage(SDK::ASBZPlayerCharacter* pCharacter, bool bEnabled)
			{
				if (!pCharacter)
				{
					m_bBlockDamageApplied = false;
					return;
				}
				if (bEnabled)
				{
					if (!m_bBlockDamageApplied)
						m_bOriginalCanBeDamaged = pCharacter->bCanBeDamaged;
					pCharacter->bCanBeDamaged = false;
					m_bBlockDamageApplied = true;
				}
				else if (m_bBlockDamageApplied)
				{
					pCharacter->bCanBeDamaged = m_bOriginalCanBeDamaged;
					m_bBlockDamageApplied = false;
				}
			}

			static void ApplyMontageRate(stMontageRate& rState, SDK::UAnimMontage* pMontage, bool bEnabled)
			{
				if (!Memory::IsValidObjectPtr(pMontage))
				{
					rState = {};
					return;
				}
				if (bEnabled)
				{
					if (pMontage != rState.pMontage)
					{
						rState.pMontage = pMontage;
						rState.flOriginalRate = pMontage->RateScale;
						rState.bApplied = false;
					}
					pMontage->RateScale = kInstantRate;
					rState.bApplied = true;
				}
				else if (rState.bApplied)
				{
					if (pMontage == rState.pMontage)
						pMontage->RateScale = rState.flOriginalRate;
					rState = {};
				}
			}

			void CompleteInteraction(SDK::ASBZPlayerCharacter* pCharacter)
			{
				SDK::USBZPlayerInteractorComponent* pInteractor = pCharacter->Interactor;
				if (!Memory::IsValidObjectPtr(pInteractor))
					return;
				SDK::USBZBaseInteractableComponent* pInteraction = pInteractor->GetCurrentInteraction();
				if (!Memory::IsValidObjectPtr(pInteraction))
				{
					m_pLastInteraction = nullptr;
					return;
				}
				if (pInteraction == m_pLastInteraction && pInteractor->InteractId == m_iLastInteractId)
					return;

				SDK::AActor* pOwner = pInteraction->GetOwner();
				if (Memory::IsValidObjectPtr(pOwner) && pOwner->IsA(SDK::ASBZMiniGameActor::StaticClass()))
					return;
				const bool bBag = Memory::IsValidObjectPtr(pOwner) && pOwner->IsA(SDK::ASBZBagItem::StaticClass());
				if (!(bBag ? *m_pInstantBagGrab : *m_pInstantInteraction))
					return;

				m_pLastInteraction = pInteraction;
				m_iLastInteractId = pInteractor->InteractId;
				pInteractor->Server_CompleteInteraction(pInteraction, pInteractor->InteractId);
				if (!SDK::UKismetSystemLibrary::IsStandalone(Unreal::GetWorld()))
					pInteractor->Multicast_CompletedInteraction(pInteraction, false);
			}

			struct stTeammateInteract
			{
				SDK::USBZBaseInteractableComponent* pInteraction = nullptr;
				std::int32_t iInteractId = -1;
			};

			static std::vector<SDK::ASBZPlayerCharacter*> Teammates(SDK::ASBZPlayerCharacter* pLocal)
			{
				std::vector<SDK::ASBZPlayerCharacter*> aTeammates;
				SDK::AGameStateBase* pGameState = Unreal::GetGameState();
				if (!pGameState)
					return aTeammates;
				SDK::TArray<SDK::APlayerState*>& aPlayers = pGameState->PlayerArray;
				for (int iPlayer = 0; iPlayer < aPlayers.Num(); ++iPlayer)
				{
					SDK::APlayerState* pState = aPlayers[iPlayer];
					if (!Memory::IsValidObjectPtr(pState))
						continue;
					SDK::APawn* pPawn = pState->PawnPrivate;
					if (!Memory::IsValidObjectPtr(pPawn) || pPawn == pLocal || !pPawn->IsA(SDK::ASBZPlayerCharacter::StaticClass()))
						continue;
					aTeammates.push_back(static_cast<SDK::ASBZPlayerCharacter*>(pPawn));
				}
				return aTeammates;
			}

			void RunTeammates(SDK::ASBZPlayerCharacter* pLocal)
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				const bool bHost = pWorld && SDK::UKismetSystemLibrary::IsServer(pWorld) && !SDK::UKismetSystemLibrary::IsStandalone(pWorld);
				m_bIsHost = bHost;
				std::vector<SDK::APlayerState*> aBring;
				{
					std::scoped_lock lckBring(m_mtxBring);
					aBring.swap(m_aBringRequests);
				}
				if (pWorld != m_pTeamWorld)
				{
					m_pTeamWorld = pWorld;
					m_mapTeammateInteract.clear();
					m_setProtected.clear();
				}
				if (!bHost)
					return;

				const std::vector<SDK::ASBZPlayerCharacter*> aTeammates = Teammates(pLocal);
				if (!*m_pTeamGodMode && !m_setProtected.empty())
				{
					for (SDK::ASBZPlayerCharacter* pTeammate : aTeammates)
						if (m_setProtected.contains(pTeammate))
							pTeammate->bCanBeDamaged = true;
					m_setProtected.clear();
				}
				for (SDK::ASBZPlayerCharacter* pTeammate : aTeammates)
				{
					SDK::USBZPlayerAttributeSet* pAttributes = GetAttributes(pTeammate);
					if (*m_pTeamGodMode)
					{
						pTeammate->bCanBeDamaged = false;
						m_setProtected.insert(pTeammate);
						if (pAttributes && pTeammate->bIsAlive)
							pAttributes->Health.CurrentValue = pAttributes->HealthMax.CurrentValue;
					}
					if (*m_pTeamInfAmmo && pAttributes)
						for (SDK::FGameplayAttributeData* pAmmo : AmmoAttributes(pAttributes))
							pAmmo->CurrentValue = 999.f;
					if (*m_pTeamInstantInteraction)
						CompleteTeammateInteraction(pTeammate);
				}

				if (aBring.empty() || !pLocal)
					return;
				const SDK::FVector vecOrigin = pLocal->K2_GetActorLocation();
				int iSlot = 0;
				for (SDK::ASBZPlayerCharacter* pTeammate : aTeammates)
				{
					SDK::APlayerState* pState = pTeammate->PlayerState;
					if (std::ranges::find(aBring, nullptr) == aBring.end() && std::ranges::find(aBring, pState) == aBring.end())
						continue;
					const double dAngle = (iSlot++ * 2.0944) + 0.5;
					const SDK::FVector vecTarget = vecOrigin + SDK::FVector(std::cos(dAngle) * 120.0, std::sin(dAngle) * 120.0, 0.0);
					pTeammate->K2_SetActorLocation(vecTarget, false, nullptr, true);
					LogInfo("teammates: brought {} to the host", pState ? pState->GetPlayerName().ToString() : std::string("player"));
				}
			}

			void CompleteTeammateInteraction(SDK::ASBZPlayerCharacter* pTeammate)
			{
				SDK::USBZPlayerInteractorComponent* pInteractor = pTeammate->Interactor;
				if (!Memory::IsValidObjectPtr(pInteractor))
					return;
				stTeammateInteract& rTrack = m_mapTeammateInteract[pTeammate];
				SDK::USBZBaseInteractableComponent* pInteraction = pInteractor->GetCurrentInteraction();
				if (!Memory::IsValidObjectPtr(pInteraction))
				{
					rTrack = {};
					return;
				}
				if (pInteraction == rTrack.pInteraction && pInteractor->InteractId == rTrack.iInteractId)
					return;
				SDK::AActor* pOwner = pInteraction->GetOwner();
				if (Memory::IsValidObjectPtr(pOwner) && (pOwner->IsA(SDK::ASBZMiniGameActor::StaticClass()) || pOwner->IsA(SDK::ASBZBagItem::StaticClass())))
					return;
				rTrack = { pInteraction, pInteractor->InteractId };
				pInteractor->Server_CompleteInteraction(pInteraction, pInteractor->InteractId);
				pInteractor->Multicast_CompletedInteraction(pInteraction, false);
			}

			static void CompleteMinigame(SDK::ASBZPlayerCharacter* pCharacter)
			{
				SDK::ASBZPlayerState* pState = pCharacter->SBZPlayerState;
				if (!Memory::IsValidObjectPtr(pState))
					return;
				const SDK::EPD3MiniGameState eState = pState->MiniGameState;
				if (eState == SDK::EPD3MiniGameState::Initiated || eState == SDK::EPD3MiniGameState::InProgress)
					pState->Server_SetMiniGameState(SDK::EPD3MiniGameState::Success);
			}

			void GrabThroughWalls(SDK::ASBZPlayerCharacter* pCharacter)
			{
				SDK::USBZPlayerInteractorComponent* pInteractor = pCharacter->Interactor;
				SDK::UWorld* pWorld = Unreal::GetWorld();
				if (!Memory::IsValidObjectPtr(pInteractor) || !pWorld)
					return;
				if (pWorld != m_pReachWorld || m_setGrabbed.size() > 512)
				{
					m_pReachWorld = pWorld;
					m_setGrabbed.clear();
				}

				const SDK::FVector vecOrigin = pCharacter->K2_GetActorLocation();
				SDK::USBZBaseInteractableComponent* pBest = nullptr;
				double dBest = 0.0;
				Payday3::ForEachLevelActor(pWorld, [&](SDK::AActor* pActor) {
					const Payday3::stItemInfo& rItem = Payday3::ResolveItem(pActor);
					if (rItem.eCategory != Payday3::EItemCategory::Loot && rItem.eCategory != Payday3::EItemCategory::KeyItem)
						return;
					if (rItem.eType == Payday3::EItemType::DepositBox || !Payday3::IsAliveActor(pActor) || pActor->IsA(SDK::ASBZMiniGameActor::StaticClass()))
						return;
					auto* pInteractable = static_cast<SDK::USBZBaseInteractableComponent*>(pActor->GetComponentByClass(SDK::USBZBaseInteractableComponent::StaticClass()));
					if (!Payday3::IsAlive(pInteractable) || pInteractable->bIsHidden || m_setGrabbed.contains(pInteractable) || pInteractable->GetOwner() != pActor)
						return;
					if (pInteractable->IsA(SDK::USBZInteractableComponent::StaticClass()) && !static_cast<SDK::USBZInteractableComponent*>(pInteractable)->bInteractionEnabled)
						return;
					const double dRange = pInteractable->NormalDistance > 0.f ? pInteractable->NormalDistance : 200.0;
					const double dDistance = (pActor->K2_GetActorLocation() - vecOrigin).Magnitude();
					if (dDistance > dRange || (pBest && dDistance >= dBest))
						return;
					pBest = pInteractable;
					dBest = dDistance;
				});
				if (!pBest)
					return;

				const std::int32_t iId = pInteractor->InteractId + 1;
				pInteractor->InteractId = iId;
				m_setGrabbed.insert(pBest);
				const std::string strName = pBest->GetOwner()->GetName();
				pInteractor->Server_StartInteraction(pBest, iId, 0);
				pInteractor->Server_CompleteInteraction(pBest, iId);
				LogInfo("reach: picked up {} at {:.0f} cm", strName, dBest);
			}

			void SecureThrownBags()
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				if (!pWorld || !SDK::UKismetSystemLibrary::IsServer(pWorld))
					return;
				if (pWorld != m_pSecureWorld)
				{
					m_pSecureWorld = pWorld;
					m_setHandledBags.clear();
				}

				std::vector<SDK::ASBZBagTriggerVolume*> aVolumes;
				std::vector<SDK::ASBZBagItem*> aBags;
				Payday3::ForEachLevelActor(pWorld, [&](SDK::AActor* pActor) {
					if (SDK::ASBZBagTriggerVolume* pVolume = Payday3::AsDropOff(pActor))
					{
						if (pVolume->GetActorEnableCollision())
							aVolumes.push_back(pVolume);
						return;
					}
					if (!pActor->IsA(SDK::ASBZBagItem::StaticClass()))
						return;
					auto* pBag = static_cast<SDK::ASBZBagItem*>(pActor);
					if (Payday3::IsAliveActor(pBag) && pBag->bIsInAir && !pBag->GetAttachParentActor() && !m_setHandledBags.contains(pBag))
						aBags.push_back(pBag);
				});
				if (aBags.empty() || aVolumes.empty())
					return;

				for (SDK::ASBZBagItem* pBag : aBags)
				{
					const SDK::FVector vecBag = pBag->K2_GetActorLocation();
					std::ranges::sort(aVolumes, {}, [&](SDK::ASBZBagTriggerVolume* pVolume) { return (pVolume->K2_GetActorLocation() - vecBag).Magnitude(); });
					m_setHandledBags.insert(pBag);
					pBag->K2_SetActorLocation(aVolumes.front()->K2_GetActorLocation(), false, nullptr, true);
					LogInfo("auto secure: bag {} moved into {}", pBag->BagId, aVolumes.front()->GetName());
				}
				if (m_setHandledBags.size() > 512)
					m_setHandledBags.clear();
			}

			void ApplyName(SDK::ASBZPlayerCharacter* pCharacter)
			{
				SDK::ASBZPlayerState* pState = pCharacter->SBZPlayerState;
				if (!Memory::IsValidObjectPtr(pState))
					return;
				if (!m_bOriginalNameSaved)
				{
					m_wstrOriginalName = pState->AccelByteDisplayName.ToWString();
					m_bOriginalNameSaved = true;
				}
				std::wstring wstrName = m_bResetName ? m_wstrOriginalName : UtfN::StringToWString<std::string>(*m_pCustomName);
				if (wstrName.size() > 32)
					wstrName.resize(32);
				if (wstrName.empty())
					return;
				pState->Multicast_SetAccelByteDisplayName(SDK::FString(wstrName.c_str()));
			}

			static void ClearDetection(SDK::ASBZPlayerCharacter* pCharacter)
			{
				for (int iDetector = 0; iDetector < pCharacter->VisualDetectors.Num(); ++iDetector)
				{
					auto* pDetector = pCharacter->VisualDetectors[iDetector];
					if (!Memory::IsValidObjectPtr(pDetector))
						continue;
					for (int iEnemy = 0; iEnemy < pDetector->EnemyDetectionValue.Num(); ++iEnemy)
						pDetector->EnemyDetectionValue[iEnemy].Target = nullptr;
					pDetector->bMarkAsCriminalOnSearch = false;
					pDetector->bShouldDisplayDetectionBuildup = false;
				}
			}

			void BackupAttributes(SDK::ASBZPlayerCharacter* pCharacter)
			{
				SDK::USBZPlayerAttributeSet* pAttributes = GetAttributes(pCharacter);
				if (!pAttributes)
					return;
				const std::size_t uKey = std::hash<std::string>{}(pCharacter->Name.ToString());
				if (m_mapAttributes.contains(uKey))
					return;
				stAttributeBackup stBackup{ .flHealthMax = pAttributes->HealthMax.CurrentValue };
				const auto aAmmo = AmmoAttributes(pAttributes);
				for (std::size_t uIndex = 0; uIndex < aAmmo.size(); ++uIndex)
					stBackup.aAmmo[uIndex] = aAmmo[uIndex]->CurrentValue;
				m_mapAttributes.emplace(uKey, stBackup);
			}

			const stAttributeBackup* FindAttributes(SDK::ASBZPlayerCharacter* pCharacter) const
			{
				if (!pCharacter)
					return nullptr;
				auto itBackup = m_mapAttributes.find(std::hash<std::string>{}(pCharacter->Name.ToString()));
				return itBackup != m_mapAttributes.end() ? &itBackup->second : nullptr;
			}

			void RestoreHealth(SDK::ASBZPlayerCharacter* pCharacter)
			{
				SDK::USBZPlayerAttributeSet* pAttributes = GetAttributes(pCharacter);
				if (const stAttributeBackup* pBackup = FindAttributes(pCharacter); pAttributes && pBackup)
					pAttributes->Health.CurrentValue = pBackup->flHealthMax;
			}

			void RestoreAmmo(SDK::ASBZPlayerCharacter* pCharacter)
			{
				SDK::USBZPlayerAttributeSet* pAttributes = GetAttributes(pCharacter);
				const stAttributeBackup* pBackup = FindAttributes(pCharacter);
				if (!pAttributes || !pBackup)
					return;
				const auto aAmmo = AmmoAttributes(pAttributes);
				for (std::size_t uIndex = 0; uIndex < aAmmo.size(); ++uIndex)
					aAmmo[uIndex]->CurrentValue = pBackup->aAmmo[uIndex];
			}

			bool BackupWeaponData()
			{
				auto* pGameInstance = static_cast<SDK::USBZGameInstance*>(Unreal::GetGameInstance());
				if (!pGameInstance || !pGameInstance->IsA(SDK::USBZGameInstance::StaticClass()))
					return false;

				SDK::USBZGlobalItemDatabase* pDatabase = pGameInstance->GlobalItemDatabase;
				if (!Memory::IsValidObjectPtr(pDatabase))
					return false;

				const std::array<SDK::USBZWeaponDatabase*, 3> aDatabases = { pDatabase->PrimaryWeapons.Get(), pDatabase->SecondaryWeapons.Get(), pDatabase->OverkillWeapons.Get() };
				if (std::ranges::any_of(aDatabases, [](SDK::USBZWeaponDatabase* pWeapons) { return !Memory::IsValidObjectPtr(pWeapons); }))
					return false;

				for (SDK::USBZWeaponDatabase* pWeapons : aDatabases)
				{
					for (int iWeapon = 0; iWeapon < pWeapons->Weapons.Num(); ++iWeapon)
					{
						auto* pWeaponData = reinterpret_cast<SDK::USBZRangedWeaponData*>(pWeapons->Weapons[iWeapon]);
						if (!Memory::IsValidObjectPtr(pWeaponData))
							return false;
						if (!pWeaponData->IsA(SDK::USBZRangedWeaponData::StaticClass()))
							continue;
						if (!pWeaponData->SpreadData || !pWeaponData->RecoilData || !pWeaponData->FireData)
							return false;

						const std::string strKey = Payday3::SafeSubstr(pWeaponData->Name.ToString(), 14);
						if (strKey.empty())
							continue;
						m_mapWeapons.try_emplace(std::hash<std::string>{}(strKey), stWeaponBackup{
							.flViewSpeedDeflect = pWeaponData->RecoilData->ViewKick.SpeedDeflect,
							.flGunKickBackSpeedDeflect = pWeaponData->RecoilData->GunKickBack.SpeedDeflect,
							.flGunSpeedDeflect = pWeaponData->RecoilData->GunKickXY.SpeedDeflect,
							.flInnerClusterSpreadMultiplier = pWeaponData->SpreadData->InnerClusterSpreadMultiplier,
							.flFireSpreadStart = pWeaponData->SpreadData->FireSpreadStart,
							.flFireSpreadMinCap = pWeaponData->SpreadData->FireSpreadMinCap,
							.flFireSpreadCap = pWeaponData->SpreadData->FireSpreadCap,
							.flFireSpreadIncrease = pWeaponData->SpreadData->FireSpreadIncrease,
							.flRoundsPerMinute = pWeaponData->FireData->RoundsPerMinute,
							.eFireMode = pWeaponData->FireData->FireMode,
						});
					}
				}
				return true;
			}

			const stWeaponBackup* FindWeapon(SDK::USBZRangedWeaponData* pWeaponData) const
			{
				auto* pEquippable = pWeaponData->EquippableClass.Get();
				if (!Memory::IsValidObjectPtr(pEquippable))
					return nullptr;
				const std::string strKey = Payday3::SafeSubstr(pEquippable->Name.ToString(), 16, 2);
				if (strKey.empty())
					return nullptr;
				auto itBackup = m_mapWeapons.find(std::hash<std::string>{}(strKey));
				return itBackup != m_mapWeapons.end() ? &itBackup->second : nullptr;
			}

			static std::vector<SDK::USBZRangedWeaponData*> CurrentWeapons(SDK::ASBZPlayerCharacter* pCharacter)
			{
				std::vector<SDK::USBZRangedWeaponData*> aWeapons;
				auto fnAdd = [&](SDK::UObject* pData) {
					if (!Memory::IsValidObjectPtr(pData) || !pData->IsA(SDK::USBZRangedWeaponData::StaticClass()))
						return;
					auto* pRanged = static_cast<SDK::USBZRangedWeaponData*>(pData);
					if (std::ranges::find(aWeapons, pRanged) == aWeapons.end())
						aWeapons.push_back(pRanged);
				};

				if (Memory::IsValidObjectPtr(pCharacter->FPCameraAttachment))
					fnAdd(pCharacter->FPCameraAttachment->EquippedWeaponData);

				for (int iEquippable = 0; iEquippable < pCharacter->EquippableArray.Num(); ++iEquippable)
				{
					auto* pEquippable = pCharacter->EquippableArray[iEquippable];
					if (Memory::IsValidObjectPtr(pEquippable))
						fnAdd(pEquippable->EquippableConfig.EquippableData);
				}
				return aWeapons;
			}

			void ApplyRecoil(const std::vector<SDK::USBZRangedWeaponData*>& aWeapons, bool bEnabled)
			{
				for (SDK::USBZRangedWeaponData* pWeaponData : aWeapons)
				{
					auto* pRecoil = pWeaponData->RecoilData;
					if (!Memory::IsValidObjectPtr(pRecoil))
						continue;
					if (bEnabled)
					{
						pRecoil->ViewKick.SpeedDeflect = 0.f;
						pRecoil->GunKickBack.SpeedDeflect = 0.f;
						pRecoil->GunKickXY.SpeedDeflect = 0.f;
					}
					else if (const stWeaponBackup* pBackup = FindWeapon(pWeaponData))
					{
						pRecoil->ViewKick.SpeedDeflect = pBackup->flViewSpeedDeflect;
						pRecoil->GunKickBack.SpeedDeflect = pBackup->flGunKickBackSpeedDeflect;
						pRecoil->GunKickXY.SpeedDeflect = pBackup->flGunSpeedDeflect;
					}
				}
			}

			void ApplySpread(const std::vector<SDK::USBZRangedWeaponData*>& aWeapons, bool bEnabled)
			{
				for (SDK::USBZRangedWeaponData* pWeaponData : aWeapons)
				{
					auto* pSpread = pWeaponData->SpreadData;
					if (!Memory::IsValidObjectPtr(pSpread))
						continue;
					if (bEnabled)
					{
						pSpread->InnerClusterSpreadMultiplier = 0.f;
						pSpread->FireSpreadStart = 0.f;
						pSpread->FireSpreadMinCap = 0.f;
						pSpread->FireSpreadCap = 0.f;
						pSpread->FireSpreadIncrease = 0.f;
					}
					else if (const stWeaponBackup* pBackup = FindWeapon(pWeaponData))
					{
						pSpread->InnerClusterSpreadMultiplier = pBackup->flInnerClusterSpreadMultiplier;
						pSpread->FireSpreadStart = pBackup->flFireSpreadStart;
						pSpread->FireSpreadMinCap = pBackup->flFireSpreadMinCap;
						pSpread->FireSpreadCap = pBackup->flFireSpreadCap;
						pSpread->FireSpreadIncrease = pBackup->flFireSpreadIncrease;
					}
				}
			}

			void ApplyFireRate(const std::vector<SDK::USBZRangedWeaponData*>& aWeapons, bool bEnabled)
			{
				for (SDK::USBZRangedWeaponData* pWeaponData : aWeapons)
				{
					auto* pFire = pWeaponData->FireData;
					const stWeaponBackup* pBackup = FindWeapon(pWeaponData);
					if (!Memory::IsValidObjectPtr(pFire) || !pBackup)
						continue;
					if (bEnabled)
					{
						pFire->RoundsPerMinute = pBackup->flRoundsPerMinute * static_cast<float>(*m_pFireRateMultiplier);
						pFire->FireMode = SDK::ESBZFireMode::Auto;
					}
					else
					{
						pFire->RoundsPerMinute = pBackup->flRoundsPerMinute;
						pFire->FireMode = pBackup->eFireMode;
					}
				}
			}

			bool* m_pGodMode = nullptr;
			int* m_pGodModeType = nullptr;
			bool* m_pInfStamina = nullptr;
			bool* m_pInstantMelee = nullptr;
			bool* m_pNoScreenshake = nullptr;
			bool* m_pNoFallDamage = nullptr;
			bool* m_pNoDetection = nullptr;
			bool* m_pInstantReload = nullptr;
			bool* m_pInfAmmo = nullptr;
			bool* m_pNoRecoil = nullptr;
			bool* m_pNoSpread = nullptr;
			bool* m_pFireRate = nullptr;
			int* m_pFireRateMultiplier = nullptr;
			std::atomic<bool> m_bQueued = false;
			bool* m_pInstantInteraction = nullptr;
			bool* m_pInstantBagGrab = nullptr;
			bool* m_pInstantMinigame = nullptr;
			std::string* m_pCustomName = nullptr;
			bool* m_pAutoSecure = nullptr;
			bool* m_pReach = nullptr;
			Config::stKeybind* m_pReachKey = nullptr;
			SDK::UWorld* m_pReachWorld = nullptr;
			std::unordered_set<SDK::USBZBaseInteractableComponent*> m_setGrabbed;
			SDK::UWorld* m_pSecureWorld = nullptr;
			std::unordered_set<SDK::ASBZBagItem*> m_setHandledBags;
			std::atomic<bool> m_bNameRequested = false;
			std::atomic<bool> m_bResetName = false;
			bool m_bOriginalNameSaved = false;
			std::wstring m_wstrOriginalName;
			SDK::USBZBaseInteractableComponent* m_pLastInteraction = nullptr;
			std::int32_t m_iLastInteractId = -1;
			bool* m_pTeamGodMode = nullptr;
			bool* m_pTeamInfAmmo = nullptr;
			bool* m_pTeamInstantInteraction = nullptr;
			std::atomic<bool> m_bIsHost = false;
			std::mutex m_mtxBring;
			std::vector<SDK::APlayerState*> m_aBringRequests;
			std::unordered_map<SDK::ASBZPlayerCharacter*, stTeammateInteract> m_mapTeammateInteract;
			std::unordered_set<SDK::ASBZPlayerCharacter*> m_setProtected;
			SDK::UWorld* m_pTeamWorld = nullptr;

			bool m_bPrevGodMode = false;
			int m_iPrevGodModeType = 0;
			bool m_bPrevInfAmmo = false;
			bool m_bBlockDamageApplied = false;
			bool m_bOriginalCanBeDamaged = true;
			stMontageRate m_stMelee;
			stMontageRate m_stReload;

			bool m_bWeaponsBackedUp = false;
			std::unordered_map<std::size_t, stWeaponBackup> m_mapWeapons;
			std::unordered_map<std::size_t, stAttributeBackup> m_mapAttributes;
		};

		clPlayerFeature g_clPlayerFeature;
	}
}

#endif
