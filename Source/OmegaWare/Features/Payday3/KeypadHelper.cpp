#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "SDK/BP_ConnectedCableBox_classes.hpp"
#include "SDK/G_Cookie_Scripting_classes.hpp"
#include "SDK/G_NightClub_Scripting_classes.hpp"
#include "SDK/Starbreeze_parameters.hpp"

#include "Config/Config.hpp"
#include "Core/Log.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

namespace OmegaWare
{
	namespace
	{
		constexpr std::chrono::milliseconds kRefreshInterval{ 1000 };
		constexpr std::chrono::milliseconds kUpdateInterval{ 100 };
		constexpr int kDisableAllWires = 99;
				constexpr int kClearKey = 10;
		constexpr int kEnterKey = 11;

		struct stKeypad
		{
			SDK::ASBZKeypadBase* pKeypad = nullptr;
			std::int32_t iIndex = -1;
		};

		struct stStation
		{
			SDK::ASBZCookingStation* pStation = nullptr;
			std::int32_t iIndex = -1;
			bool bRestricted = false;
			int iLoggedRequest = -2;
			int iLoggedState = -1;
			bool bLoggedScript = false;
		};

		[[nodiscard]] int ActiveKey(const SDK::ASBZKeypadBase* pKeypad)
		{
			switch (pKeypad->Inputs)
			{
			case 0:
				return pKeypad->Code / 1000;
			case 1:
				return (pKeypad->Code / 100) % 10;
			case 2:
				return (pKeypad->Code / 10) % 10;
			case 3:
				return pKeypad->Code % 10;
			default:
				return pKeypad->GuessedCode != pKeypad->Code ? kClearKey : kEnterKey;
			}
		}

		void SetKeysEnabled(SDK::TArray<SDK::USBZInteractableComponent*>& rKeys, int iOnly)
		{
			for (int iKey = 0; iKey < rKeys.Num(); ++iKey)
				if (Payday3::IsAlive(rKeys[iKey]))
					rKeys[iKey]->SetLocalEnabled(iOnly < 0 || iKey == iOnly);
		}

		struct stWireBox
		{
			SDK::ASBZConnectedCableBox* pBox = nullptr;
			std::int32_t iIndex = -1;
			bool bRestricted = false;
			std::int32_t iLoggedState = -9999;
		};

		[[nodiscard]] int CorrectWire(const SDK::ASBZConnectedCableBox* pBox)
		{
			if (pBox->bIsCompleted || pBox->bIsDoorOpen)
				return -1;
			const int iStep = pBox->CurrentSequenceIndex;
			const int iCorrect = iStep >= 0 && iStep < pBox->CurrentCorrectSequence.Num() ? pBox->CurrentCorrectSequence[iStep] : pBox->CurrentCorrectColorIndex;
			return iCorrect >= 0 && iCorrect < pBox->CableInteractables.Num() ? iCorrect : -1;
		}

		void SetWiresEnabled(SDK::ASBZConnectedCableBox* pBox, int iOnly)
		{
			SetKeysEnabled(pBox->CableInteractables, iOnly);
		}

		class clKeypadHelperFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "keypadhelper"; }

			bool Setup() override
			{
				m_pEnabled = &Config::Add("keypadhelper.enabled", false);
				m_pMethLab = &Config::Add("keypadhelper.methlab", false);
				m_pWireSwitch = &Config::Add("keypadhelper.wireswitch", false);
				m_hIngredientAdded = Unreal::OnFunction<SDK::Params::SBZCookingStation_BP_OnIngredientAdded>("Starbreeze.SBZCookingStation.BP_OnIngredientAdded",
					[](SDK::UObject* pObject, SDK::Params::SBZCookingStation_BP_OnIngredientAdded& rParams, bool&) {
						LogInfo("meth helper: {} added ingredient index {}", pObject ? pObject->GetName() : std::string("?"), rParams.IngredientIndex);
					});
				m_hIngredientAck = Unreal::OnFunction<SDK::Params::SBZCookingStation_OnAckIngredientAdded>("Starbreeze.SBZCookingStation.OnAckIngredientAdded",
					[](SDK::UObject* pObject, SDK::Params::SBZCookingStation_OnAckIngredientAdded& rParams, bool&) {
						LogInfo("meth helper: {} ack for {} locally controlled {}", pObject ? pObject->GetName() : std::string("?"), Payday3::IsAlive(rParams.Comp) ? rParams.Comp->GetName() : std::string("null"), static_cast<bool>(rParams.bInIsLocallyControlled));
					});
				return true;
			}

			void Destroy() override
			{
				if (m_hIngredientAdded != Unreal::kInvalidHook)
					Unreal::RemoveProcessEvent(m_hIngredientAdded);
				m_hIngredientAdded = Unreal::kInvalidHook;
				if (m_hIngredientAck != Unreal::kInvalidHook)
					Unreal::RemoveProcessEvent(m_hIngredientAck);
				m_hIngredientAck = Unreal::kInvalidHook;
				if (Payday3::IsReady())
					Unreal::RunOnGameThreadAndWait([this] { RestoreKeypads(); RestoreStations(); RestoreWireBoxes(); }, std::chrono::seconds(1));
			}

			void Run() override
			{
				using Clock_t = std::chrono::steady_clock;
				if ((!*m_pEnabled && !*m_pMethLab && !*m_pWireSwitch && !m_bApplied) || !Payday3::IsReady() || Clock_t::now() < m_tpNextUpdate || m_bQueued.exchange(true))
					return;
				m_tpNextUpdate = Clock_t::now() + kUpdateInterval;
				if (!Unreal::RunOnGameThread([this] {
						m_bQueued = false;
						Features::Guard(*this, "game thread", [this] { Tick(); });
					}))
					m_bQueued = false;
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				if (OmegaGUI::BeginSection("keypadhelper.section"_l))
				{
					OmegaGUI::Checkbox("keypadhelper.enabled"_l, m_pEnabled);
					OmegaGUI::ItemTooltip("keypadhelper.enabled_hint"_t);
					OmegaGUI::Checkbox("keypadhelper.methlab"_l, m_pMethLab);
					OmegaGUI::ItemTooltip("keypadhelper.methlab_hint"_t);
					OmegaGUI::Checkbox("keypadhelper.wireswitch"_l, m_pWireSwitch);
					OmegaGUI::ItemTooltip("keypadhelper.wireswitch_hint"_t);
					OmegaGUI::EndSection();
				}
			}

		private:
			void Tick()
			{
				if (!IsActive())
				{
					RestoreKeypads();
					RestoreStations();
					RestoreWireBoxes();
					return;
				}
				if (std::chrono::steady_clock::now() >= m_tpNextRefresh)
					Refresh();
				if (*m_pEnabled)
					UpdateKeypads();
				else
					RestoreKeypads();
				if (*m_pMethLab)
					UpdateStations();
				else
					RestoreStations();
				if (*m_pWireSwitch)
					UpdateWireBoxes();
				else
					RestoreWireBoxes();
				m_bApplied = m_bKeypadsRestricted || std::ranges::any_of(m_aStations, [](const stStation& rStation) { return rStation.bRestricted; }) || std::ranges::any_of(m_aWireBoxes, [](const stWireBox& rBox) { return rBox.bRestricted; });
			}

			[[nodiscard]] bool IsScriptClass(const SDK::UClass* pClass)
			{
				if (const auto itKnown = m_mapScriptClasses.find(pClass); itKnown != m_mapScriptClasses.end())
					return itKnown->second;
				if (m_mapScriptClasses.size() > 1024)
					m_mapScriptClasses.clear();
				bool bScript = false;
				for (const SDK::UStruct* pStruct = pClass; pStruct && !bScript; pStruct = pStruct->SuperStruct)
					bScript = pStruct->Name.ToString() == "G_Cookie_Scripting_C";
				m_mapScriptClasses.emplace(pClass, bScript);
				return bScript;
			}

			[[nodiscard]] bool IsClubScriptClass(const SDK::UClass* pClass)
			{
				if (const auto itKnown = m_mapClubScriptClasses.find(pClass); itKnown != m_mapClubScriptClasses.end())
					return itKnown->second;
				if (m_mapClubScriptClasses.size() > 1024)
					m_mapClubScriptClasses.clear();
				bool bScript = false;
				for (const SDK::UStruct* pStruct = pClass; pStruct && !bScript; pStruct = pStruct->SuperStruct)
					bScript = pStruct->Name.ToString() == "G_NightClub_Scripting_C";
				m_mapClubScriptClasses.emplace(pClass, bScript);
				return bScript;
			}

			void Refresh()
			{
				std::vector<SDK::ASBZCookingStation*> aOldStations;
				std::vector<stStation> aKept;
				for (const stStation& rStation : m_aStations)
					aOldStations.push_back(rStation.pStation);
				std::vector<stKeypad> aKeypads;
				std::vector<SDK::ASBZCookingStation*> aStations;
				std::vector<SDK::ASBZConnectedCableBox*> aWireBoxes;
				SDK::AG_Cookie_Scripting_C* pScript = nullptr;
				SDK::AG_NightClub_Scripting_C* pClub = nullptr;
				Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
					if (!Payday3::IsAliveActor(pActor))
						return;
					if (pActor->IsA(SDK::ASBZKeypadBase::StaticClass()))
						aKeypads.push_back({ static_cast<SDK::ASBZKeypadBase*>(pActor), pActor->Index });
					else if (pActor->IsA(SDK::ASBZCookingStation::StaticClass()))
						aStations.push_back(static_cast<SDK::ASBZCookingStation*>(pActor));
					else if (*m_pWireSwitch && pActor->IsA(SDK::ASBZConnectedCableBox::StaticClass()))
						aWireBoxes.push_back(static_cast<SDK::ASBZConnectedCableBox*>(pActor));
					else if (*m_pMethLab && pActor->Class && IsScriptClass(pActor->Class))
						pScript = static_cast<SDK::AG_Cookie_Scripting_C*>(pActor);
					else if (*m_pWireSwitch && pActor->Class && IsClubScriptClass(pActor->Class))
						pClub = static_cast<SDK::AG_NightClub_Scripting_C*>(pActor);
				});
				for (SDK::ASBZCookingStation* pStation : aStations)
				{
					const auto itOld = std::ranges::find(m_aStations, pStation, &stStation::pStation);
					aKept.push_back(itOld != m_aStations.end() && itOld->iIndex == pStation->Index ? *itOld : stStation{ pStation, pStation->Index });
				}
				for (const stStation& rStation : m_aStations)
					if (rStation.bRestricted && std::ranges::find(aStations, rStation.pStation) == aStations.end() && Payday3::IsSameObject(rStation.pStation, rStation.iIndex))
						SetKeysEnabled(rStation.pStation->IngredientInteractableArray, -1);
				std::vector<stWireBox> aKeptBoxes;
				for (SDK::ASBZConnectedCableBox* pBox : aWireBoxes)
				{
					const auto itOld = std::ranges::find(m_aWireBoxes, pBox, &stWireBox::pBox);
					aKeptBoxes.push_back(itOld != m_aWireBoxes.end() && itOld->iIndex == pBox->Index ? *itOld : stWireBox{ pBox, pBox->Index });
				}
				m_aWireBoxes = std::move(aKeptBoxes);
				m_aKeypads = std::move(aKeypads);
				m_aStations = std::move(aKept);
				m_pScript = pScript;
				m_pClubScript = pClub;
				m_tpNextRefresh = std::chrono::steady_clock::now() + kRefreshInterval;
			}

			void UpdateKeypads()
			{
				for (const stKeypad& rKeypad : m_aKeypads)
					if (Payday3::IsSameObject(rKeypad.pKeypad, rKeypad.iIndex))
						SetKeysEnabled(rKeypad.pKeypad->KeypadInteractableComponentArray, ActiveKey(rKeypad.pKeypad));
				m_bKeypadsRestricted = true;
			}

			void RestoreKeypads()
			{
				if (!m_bKeypadsRestricted)
					return;
				for (const stKeypad& rKeypad : m_aKeypads)
					if (Payday3::IsSameObject(rKeypad.pKeypad, rKeypad.iIndex))
						SetKeysEnabled(rKeypad.pKeypad->KeypadInteractableComponentArray, -1);
				m_bKeypadsRestricted = false;
			}

			void UpdateStations()
			{
				const bool bRequest = Payday3::IsAlive(m_pScript) && m_pScript->IngredientRequestActive;
				const int iRequested = bRequest ? m_pScript->New_Ingredient_Index : -1;
				for (stStation& rStation : m_aStations)
				{
					if (!Payday3::IsSameObject(rStation.pStation, rStation.iIndex))
						continue;
					SDK::TArray<SDK::USBZInteractableComponent*>& rKeys = rStation.pStation->IngredientInteractableArray;
					const bool bWaiting = rStation.pStation->CurrentState == SDK::ESBZCookingState::WaitingForIngredients;
					const bool bRestrict = bWaiting && iRequested >= 1 && iRequested <= rKeys.Num();
					const int iState = static_cast<int>(rStation.pStation->CurrentState);
					if (iRequested != rStation.iLoggedRequest || iState != rStation.iLoggedState || bRequest != rStation.bLoggedScript)
					{
						rStation.iLoggedRequest = iRequested;
						rStation.iLoggedState = iState;
						rStation.bLoggedScript = bRequest;
						std::string strKeys;
						for (int iKey = 0; iKey < rKeys.Num(); ++iKey)
							strKeys += std::format("{}{}:{}", iKey ? ", " : "", iKey, Payday3::IsAlive(rKeys[iKey]) ? rKeys[iKey]->GetName() : std::string("null"));
						LogInfo("meth helper: {} state {} script {} requested index {} (raw {}) seed {} static order {} keys [{}] -> {}", rStation.pStation->GetName(), iState, Payday3::IsAlive(m_pScript) ? "found" : "missing", iRequested,
							Payday3::IsAlive(m_pScript) ? m_pScript->New_Ingredient_Index : -1, rStation.pStation->Seed, static_cast<bool>(rStation.pStation->bIsOrderStatic), strKeys, bRestrict ? std::format("only key {} enabled", iRequested - 1) : std::string("no restriction"));
					}
					if (bRestrict)
						SetKeysEnabled(rKeys, iRequested - 1);
					else if (rStation.bRestricted)
						SetKeysEnabled(rKeys, -1);
					rStation.bRestricted = bRestrict;
				}
			}

			void UpdateWireBoxes()
			{
				for (stWireBox& rBox : m_aWireBoxes)
				{
					if (!Payday3::IsSameObject(rBox.pBox, rBox.iIndex))
						continue;
					int iCorrect = CorrectWire(rBox.pBox);
					const SDK::ASBZConnectedCableBox* pCorrectOne = Payday3::IsAlive(m_pClubScript) ? m_pClubScript->CorrectCableBox1 : nullptr;
					const SDK::ASBZConnectedCableBox* pCorrectTwo = Payday3::IsAlive(m_pClubScript) ? m_pClubScript->CorrectCableBox2 : nullptr;
					const bool bScriptKnowsBoxes = Payday3::IsAlive(pCorrectOne) || Payday3::IsAlive(pCorrectTwo);
					const bool bScriptCorrect = rBox.pBox == pCorrectOne || rBox.pBox == pCorrectTwo;
					if (bScriptKnowsBoxes && !bScriptCorrect && !rBox.pBox->bIsCompleted && !rBox.pBox->bIsDoorOpen)
						iCorrect = kDisableAllWires;
					const std::int32_t iState = iCorrect * 1000 + rBox.pBox->CurrentSequenceIndex * 10 + (rBox.pBox->bIsCompleted ? 1 : 0) + (rBox.pBox->bIsDoorOpen ? 2 : 0) + rBox.pBox->FailCounter * 100000;
					if (iState != rBox.iLoggedState)
					{
						rBox.iLoggedState = iState;
						std::string strSequence;
						for (int iStep = 0; iStep < rBox.pBox->CurrentCorrectSequence.Num(); ++iStep)
							strSequence += std::format("{} ", rBox.pBox->CurrentCorrectSequence[iStep]);
						LogInfo("wire helper: {} ({}) correct wire {}, step {}, sequence [{}], color index {}, wires {}, completed {}, door open {}, fails {}, script knows boxes {}, script picked this box {}", rBox.pBox->GetName(), rBox.pBox->Class ? rBox.pBox->Class->GetName() : std::string(), iCorrect, rBox.pBox->CurrentSequenceIndex, strSequence, rBox.pBox->CurrentCorrectColorIndex, rBox.pBox->CableInteractables.Num(), rBox.pBox->bIsCompleted, rBox.pBox->bIsDoorOpen, static_cast<int>(rBox.pBox->FailCounter), bScriptKnowsBoxes, bScriptCorrect);
					}
					if (iCorrect >= 0)
						SetWiresEnabled(rBox.pBox, iCorrect);
					else if (rBox.bRestricted && !rBox.pBox->bIsCompleted && !rBox.pBox->bIsDoorOpen)
						SetWiresEnabled(rBox.pBox, -1);
					rBox.bRestricted = iCorrect >= 0;
				}
			}

			void RestoreWireBoxes()
			{
				for (stWireBox& rBox : m_aWireBoxes)
				{
					if (rBox.bRestricted && Payday3::IsSameObject(rBox.pBox, rBox.iIndex) && !rBox.pBox->bIsCompleted && !rBox.pBox->bIsDoorOpen)
						SetWiresEnabled(rBox.pBox, -1);
					rBox.bRestricted = false;
				}
			}

			void RestoreStations()
			{
				for (stStation& rStation : m_aStations)
					if (rStation.bRestricted && Payday3::IsSameObject(rStation.pStation, rStation.iIndex))
						SetKeysEnabled(rStation.pStation->IngredientInteractableArray, -1);
				for (stStation& rStation : m_aStations)
					rStation.bRestricted = false;
			}

			Unreal::HookHandle m_hIngredientAdded = Unreal::kInvalidHook;
			Unreal::HookHandle m_hIngredientAck = Unreal::kInvalidHook;
			bool* m_pEnabled = nullptr;
			bool* m_pMethLab = nullptr;
			bool* m_pWireSwitch = nullptr;
			bool m_bKeypadsRestricted = false;
			std::atomic<bool> m_bQueued = false;
			std::atomic<bool> m_bApplied = false;
			std::chrono::steady_clock::time_point m_tpNextUpdate;
			std::chrono::steady_clock::time_point m_tpNextRefresh;
			std::vector<stKeypad> m_aKeypads;
			std::vector<stStation> m_aStations;
			std::vector<stWireBox> m_aWireBoxes;
			SDK::AG_Cookie_Scripting_C* m_pScript = nullptr;
			SDK::AG_NightClub_Scripting_C* m_pClubScript = nullptr;
			std::unordered_map<const SDK::UClass*, bool> m_mapClubScriptClasses;
			std::unordered_map<const SDK::UClass*, bool> m_mapScriptClasses;
		};

		clKeypadHelperFeature g_clKeypadHelperFeature;
	}
}

#endif
