#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "Config/Config.hpp"
#include "Core/Log.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Engines/Unreal/RpcValidate.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace OmegaWare
{
	namespace
	{
		struct stCompleteInteractionTag;
		using clCompleteInteractionOverride = Unreal::clValidateOverride<stCompleteInteractionTag, SDK::USBZBaseInteractableComponent*, std::int32_t>;

		struct stStartSlideTag;
		struct stStopSlideTag;
		struct stSprintTag;
		using clStartSlideOverride = Unreal::clValidateOverride<stStartSlideTag, const SDK::FVector&, const SDK::FVector&>;
		using clStopSlideOverride = Unreal::clValidateOverride<stStopSlideTag>;
		using clSprintOverride = Unreal::clValidateOverride<stSprintTag, bool>;

		struct stMovementBackup
		{
			bool bIgnoreErrorChecks = false;
			bool bAcceptClientPosition = false;
		};

		class clHostRulesFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "hostrules"; }
			[[nodiscard]] std::string_view GetGroup() const override { return "session"; }

			bool Setup() override
			{
				m_pLongInteraction = &Config::Add("hostrules.long_interaction", false);
				m_pInteractionRange = &Config::Add("hostrules.interaction_range", 1000.f);
				m_pClientMovement = &Config::Add("hostrules.client_movement", false);
				m_pAllowPickups = &Config::Add("hostrules.allow_pickups", false);
				m_pAllPlayers = &Config::Add("hostrules.all_players", false);
				m_pOmegaWareUsers = &Config::Add("hostrules.omegaware_users", true);
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
					Unreal::RunOnGameThreadAndWait([this] { RestoreMovement({}); }, std::chrono::seconds(1));
				m_clCompleteInteraction.DetachAll();
				m_clStartSlide.DetachAll();
				m_clStopSlide.DetachAll();
				m_clSprint.DetachAll();
				{
					std::scoped_lock lckArmedMovement(m_mtxArmed);
					m_setArmedMovement.clear();
				}
				std::scoped_lock lckArmed(m_mtxArmed);
				m_mapArmedInteractors.clear();
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				SDK::AGameStateBase* pGameState = Payday3::IsReady() ? Unreal::GetGameState() : nullptr;
				const bool bHost = m_bHost;
				const bool bAnyRule = true;
				OmegaGUI::BeginColumns(2);
				if (OmegaGUI::BeginSection("hostrules.section_rules"_l))
				{
					if (!bHost)
						OmegaGUI::TextMuted("hostrules.host_only"_t);
					OmegaGUI::Checkbox("hostrules.long_interaction"_l, m_pLongInteraction);
					OmegaGUI::ItemTooltip("hostrules.long_interaction_hint"_t);
					if (*m_pLongInteraction)
					{
						OmegaGUI::Indent();
						OmegaGUI::SliderFloat("hostrules.interaction_range"_l, m_pInteractionRange, 200.f, 10000.f, "{:.0f}");
						OmegaGUI::TextMuted(std::format("{}: {} ({})", m_clCompleteInteraction.GetFunctionName(), Unreal::ToString(m_clCompleteInteraction.GetStatus()), m_clCompleteInteraction.GetOverrides()));
						OmegaGUI::Unindent();
					}
					OmegaGUI::Checkbox("hostrules.client_movement"_l, m_pClientMovement);
					OmegaGUI::ItemTooltip("hostrules.client_movement_hint"_t);
					OmegaGUI::Checkbox("hostrules.allow_pickups"_l, m_pAllowPickups);
					OmegaGUI::ItemTooltip("hostrules.allow_pickups_hint"_t);
					OmegaGUI::EndSection();
				}
				OmegaGUI::NextColumn();
				if (bAnyRule && OmegaGUI::BeginSection("hostrules.section_trusted"_l))
				{
					OmegaGUI::TextMuted("hostrules.trusted_hint"_t);
					if (!pGameState)
						OmegaGUI::TextMuted("player.no_players"_t);
					else
						DrawTrusted(pGameState);
					OmegaGUI::EndSection();
				}
				OmegaGUI::EndColumns();
			}

		private:
			void DrawTrusted(SDK::AGameStateBase* pGameState)
			{
				using namespace OmegaLocalization::Literals;
				OmegaGUI::Checkbox("hostrules.omegaware_users"_l, m_pOmegaWareUsers);
				OmegaGUI::ItemTooltip("hostrules.omegaware_users_hint"_t);
				OmegaGUI::Checkbox("hostrules.all_players"_l, m_pAllPlayers);
				OmegaGUI::ItemTooltip("hostrules.all_players_hint"_t);
				const bool bAllPlayers = *m_pAllPlayers;
				const bool bOmegaWare = *m_pOmegaWareUsers;
				SDK::APlayerState* pLocalState = LocalPlayerState();
				std::scoped_lock lckTrusted(m_mtxTrusted);
				OmegaGUI::BeginDisabled(bAllPlayers);
				SDK::TArray<SDK::APlayerState*>& aPlayers = pGameState->PlayerArray;
				for (int iPlayer = 0; iPlayer < aPlayers.Num(); ++iPlayer)
				{
					SDK::APlayerState* pState = aPlayers[iPlayer];
					if (!Memory::IsValidObjectPtr(pState) || pState == pLocalState)
						continue;
					const bool bForced = bAllPlayers || (bOmegaWare && Payday3::GetModUserKind(pState) != Payday3::EModUserKind::None);
					bool bTrusted = bForced || m_setTrusted.contains(pState);
					OmegaGUI::PushID(pState);
					OmegaGUI::BeginDisabled(bForced && !bAllPlayers);
					const bool bToggled = OmegaGUI::Checkbox(pState->GetPlayerName().ToString(), &bTrusted);
					OmegaGUI::EndDisabled();
					if (bToggled && !bForced)
					{
						if (bTrusted)
							m_setTrusted.insert(pState);
						else
							m_setTrusted.erase(pState);
					}
					OmegaGUI::PopID();
				}
				OmegaGUI::EndDisabled();
			}

			static SDK::APlayerState* LocalPlayerState()
			{
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				return Memory::IsValidObjectPtr(pController) ? pController->PlayerState : nullptr;
			}

			void RunGame()
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				if (pWorld != m_pWorld)
				{
					m_pWorld = pWorld;
					m_mapMovement.clear();
					std::scoped_lock lckTrusted(m_mtxTrusted);
					m_setTrusted.clear();
				}
				const bool bHost = Unreal::IsNetHost();
				m_bHost = bHost;

				std::unordered_map<SDK::USBZPlayerInteractorComponent*, SDK::ASBZPlayerCharacter*> mapArmed;
				std::unordered_set<SDK::UCharacterMovementComponent*> setMovement;
				std::unordered_set<SDK::UObject*> setArmedMovement;
				if (bHost)
				{
					for (SDK::ASBZPlayerCharacter* pCharacter : TrustedCharacters())
					{
						SDK::USBZPlayerInteractorComponent* pInteractor = OW_FIELD(pCharacter, Interactor);
						if (Payday3::IsAlive(pInteractor) && m_clCompleteInteraction.Attach(pInteractor))
							mapArmed.emplace(pInteractor, pCharacter);
						SDK::UCharacterMovementComponent* pMovement = pCharacter->CharacterMovement;
						if (*m_pClientMovement && Payday3::IsAlive(pMovement))
							setMovement.insert(pMovement);
						if (Payday3::IsAlive(pMovement) && ArmMovement(pMovement))
							setArmedMovement.insert(pMovement);
					}
					if (SDK::ASBZPlayerCharacter* pLocal = Payday3::GetLocalCharacter())
					{
						SDK::USBZPlayerInteractorComponent* pLocalInteractor = OW_FIELD(pLocal, Interactor);
						if (Payday3::IsAlive(pLocalInteractor) && m_clCompleteInteraction.Attach(pLocalInteractor))
							mapArmed.emplace(pLocalInteractor, pLocal);
						SDK::UCharacterMovementComponent* pLocalMovement = pLocal->CharacterMovement;
						if (Payday3::IsAlive(pLocalMovement) && ArmMovement(pLocalMovement))
							setArmedMovement.insert(pLocalMovement);
					}
				}
				{
					std::scoped_lock lckArmed(m_mtxArmed);
					m_mapArmedInteractors = std::move(mapArmed);
					m_setArmedMovement = std::move(setArmedMovement);
				}
				ApplyMovement(setMovement);
			}

			bool ArmMovement(SDK::UCharacterMovementComponent* pMovement)
			{
				const bool bSlide = m_clStartSlide.Attach(pMovement);
				const bool bStop = m_clStopSlide.Attach(pMovement);
				const bool bSprint = m_clSprint.Attach(pMovement);
				return bSlide || bStop || bSprint;
			}

			bool IsArmedMovement(SDK::UObject* pObject)
			{
				std::scoped_lock lckArmed(m_mtxArmed);
				return m_setArmedMovement.contains(pObject);
			}

			std::vector<SDK::ASBZPlayerCharacter*> TrustedCharacters()
			{
				std::vector<SDK::ASBZPlayerCharacter*> aCharacters;
				SDK::AGameStateBase* pGameState = Unreal::GetGameState();
				if (!pGameState)
					return aCharacters;
				SDK::APawn* pLocalPawn = Unreal::GetLocalPawn();
				SDK::APlayerState* pLocalState = LocalPlayerState();
				const bool bAllPlayers = *m_pAllPlayers;
				const bool bOmegaWare = *m_pOmegaWareUsers;
				std::scoped_lock lckTrusted(m_mtxTrusted);
				SDK::TArray<SDK::APlayerState*>& aPlayers = pGameState->PlayerArray;
				for (int iPlayer = 0; iPlayer < aPlayers.Num(); ++iPlayer)
				{
					SDK::APlayerState* pState = aPlayers[iPlayer];
					if (!Memory::IsValidObjectPtr(pState) || pState == pLocalState || (!bAllPlayers && !m_setTrusted.contains(pState) && !(bOmegaWare && Payday3::GetModUserKind(pState) != Payday3::EModUserKind::None)))
						continue;
					SDK::APawn* pPawn = pState->PawnPrivate;
					if (!Payday3::IsAliveActor(pPawn) || pPawn == pLocalPawn || !pPawn->IsA(SDK::ASBZPlayerCharacter::StaticClass()))
						continue;
					aCharacters.push_back(static_cast<SDK::ASBZPlayerCharacter*>(pPawn));
				}
				return aCharacters;
			}

			static bool IsPickupInteraction(SDK::USBZBaseInteractableComponent* pInteraction)
			{
				SDK::AActor* pOwner = pInteraction->GetOwner();
				if (!Payday3::IsAliveActor(pOwner))
					return false;
				if (pOwner->IsA(SDK::ASBZBagItem::StaticClass()) || pOwner->IsA(SDK::ASBZInstantLoot::StaticClass()) || pOwner->IsA(SDK::ASBZTimedBagConverter::StaticClass()))
					return true;
				const Payday3::stItemInfo& rItem = Payday3::ResolveItem(pOwner);
				return (rItem.eCategory == Payday3::EItemCategory::Loot || rItem.eCategory == Payday3::EItemCategory::KeyItem) && rItem.eType != Payday3::EItemType::DepositBox && rItem.eType != Payday3::EItemType::ATM;
			}

			void LogRejection(SDK::UObject* pObject, SDK::USBZBaseInteractableComponent* pInteraction, std::string_view strReason)
			{
				using Clock_t = std::chrono::steady_clock;
				const auto tpNow = Clock_t::now();
				if (tpNow < m_tpNextRejectionLog)
					return;
				m_tpNextRejectionLog = tpNow + std::chrono::milliseconds(500);
				SDK::AActor* pOwner = Payday3::IsAlive(pObject) ? static_cast<SDK::UActorComponent*>(pObject)->GetOwner() : nullptr;
				SDK::AActor* pTarget = Payday3::IsAlive(pInteraction) ? pInteraction->GetOwner() : nullptr;
				LogWarning("hostrules: rejected a complete interaction from {} on {}: {}", Payday3::IsAliveActor(pOwner) ? pOwner->GetName() : std::string("unknown"), Payday3::IsAliveActor(pTarget) ? pTarget->GetName() : std::string("unknown"), strReason);
			}

			bool AllowCompleteInteraction(SDK::UObject* pObject, SDK::USBZBaseInteractableComponent* pInteraction, std::int32_t)
			{
				if (Payday3::IsInternalInteraction())
					return true;
				if (!Payday3::IsAlive(pInteraction))
					return false;
				SDK::ASBZPlayerCharacter* pCharacter = nullptr;
				{
					std::scoped_lock lckArmed(m_mtxArmed);
					const auto itArmed = m_mapArmedInteractors.find(static_cast<SDK::USBZPlayerInteractorComponent*>(pObject));
					if (itArmed != m_mapArmedInteractors.end())
						pCharacter = itArmed->second;
				}
				if (!pCharacter)
				{
					if (*m_pAllowPickups && IsPickupInteraction(pInteraction))
						return true;
					LogRejection(pObject, pInteraction, "player is not trusted");
					return false;
				}
				if (*m_pAllowPickups && IsPickupInteraction(pInteraction))
					return true;
				if (pCharacter == Payday3::GetLocalCharacter() || !*m_pLongInteraction)
					return true;
				SDK::AActor* pTarget = pInteraction->GetOwner();
				if (!Payday3::IsAliveActor(pCharacter) || !Payday3::IsAliveActor(pTarget))
					return false;
				const double dDistance = (pTarget->K2_GetActorLocation() - pCharacter->K2_GetActorLocation()).Magnitude();
				if (dDistance > *m_pInteractionRange)
				{
					LogRejection(pObject, pInteraction, std::format("{:.0f} cm is past the {:.0f} cm limit", dDistance, *m_pInteractionRange));
					return false;
				}
				LogDebug("hostrules: allowed {} to interact with {} from {:.0f}", pCharacter->GetName(), pTarget->GetName(), dDistance);
				return true;
			}

			void ApplyMovement(const std::unordered_set<SDK::UCharacterMovementComponent*>& setWanted)
			{
				RestoreMovement(setWanted);
				for (SDK::UCharacterMovementComponent* pMovement : setWanted)
				{
					if (!m_mapMovement.contains(pMovement))
						m_mapMovement.emplace(pMovement, stMovementBackup{ static_cast<bool>(pMovement->bIgnoreClientMovementErrorChecksAndCorrection), static_cast<bool>(pMovement->bServerAcceptClientAuthoritativePosition) });
					pMovement->bIgnoreClientMovementErrorChecksAndCorrection = true;
					pMovement->bServerAcceptClientAuthoritativePosition = true;
				}
			}

			void RestoreMovement(const std::unordered_set<SDK::UCharacterMovementComponent*>& setKeep)
			{
				for (auto itMovement = m_mapMovement.begin(); itMovement != m_mapMovement.end();)
				{
					auto& [pMovement, stBackup] = *itMovement;
					if (setKeep.contains(pMovement))
					{
						++itMovement;
						continue;
					}
					if (Payday3::IsAlive(pMovement))
					{
						pMovement->bIgnoreClientMovementErrorChecksAndCorrection = stBackup.bIgnoreErrorChecks;
						pMovement->bServerAcceptClientAuthoritativePosition = stBackup.bAcceptClientPosition;
					}
					itMovement = m_mapMovement.erase(itMovement);
				}
			}

			bool* m_pLongInteraction = nullptr;
			float* m_pInteractionRange = nullptr;
			bool* m_pClientMovement = nullptr;
			bool* m_pAllowPickups = nullptr;
			bool* m_pAllPlayers = nullptr;
			bool* m_pOmegaWareUsers = nullptr;

			clStartSlideOverride m_clStartSlide{ "Starbreeze.SBZCharacterMovementComponent.Server_StartSlide",
				[this](SDK::UObject* pObject, const SDK::FVector&, const SDK::FVector&) { return IsArmedMovement(pObject); } };
			clStopSlideOverride m_clStopSlide{ "Starbreeze.SBZCharacterMovementComponent.Server_StopSlide",
				[this](SDK::UObject* pObject) { return IsArmedMovement(pObject); } };
			clSprintOverride m_clSprint{ "Starbreeze.SBZPlayerMovementComponent.Server_SetWantToSprint",
				[this](SDK::UObject* pObject, bool) { return IsArmedMovement(pObject); } };
			std::unordered_set<SDK::UObject*> m_setArmedMovement;
			clCompleteInteractionOverride m_clCompleteInteraction{ "Starbreeze.SBZInteractorComponent.Server_CompleteInteraction",
				[this](SDK::UObject* pObject, SDK::USBZBaseInteractableComponent* pInteraction, std::int32_t iId) { return AllowCompleteInteraction(pObject, pInteraction, iId); } };

			SDK::UWorld* m_pWorld = nullptr;
			std::unordered_map<SDK::UCharacterMovementComponent*, stMovementBackup> m_mapMovement;
			std::mutex m_mtxTrusted;
			std::unordered_set<SDK::APlayerState*> m_setTrusted;
			std::mutex m_mtxArmed;
			std::unordered_map<SDK::USBZPlayerInteractorComponent*, SDK::ASBZPlayerCharacter*> m_mapArmedInteractors;
			std::chrono::steady_clock::time_point m_tpNextRejectionLog;
			std::atomic<bool> m_bHost = false;
			std::atomic<bool> m_bQueued = false;
		};

		clHostRulesFeature g_clHostRulesFeature;
	}
}

#endif
