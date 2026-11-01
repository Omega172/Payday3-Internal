#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "SDK/G_Cookie_Scripting_classes.hpp"

#include "Config/Config.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

namespace OmegaWare
{
	namespace
	{
		constexpr std::chrono::milliseconds kRefreshInterval{ 1000 };
		constexpr std::chrono::milliseconds kUpdateInterval{ 100 };
		constexpr std::chrono::seconds kClassRetryInterval{ 10 };
		constexpr int kClearKey = 10;
		constexpr int kEnterKey = 11;

		struct stStation
		{
			SDK::ASBZCookingStation* pStation = nullptr;
			bool bRestricted = false;
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
				if (Payday3::IsLiveObject(rKeys[iKey]))
					rKeys[iKey]->SetLocalEnabled(iOnly < 0 || iKey == iOnly);
		}

		class clKeypadHelperFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "keypadhelper"; }

			bool Setup() override
			{
				m_pEnabled = &Config::Add("keypadhelper.enabled", false);
				m_pMethLab = &Config::Add("keypadhelper.methlab", false);
				return true;
			}

			void Destroy() override
			{
				if (Payday3::IsReady())
					Unreal::RunOnGameThreadAndWait([this] { RestoreKeypads(); RestoreStations(); }, std::chrono::seconds(1));
			}

			void Run() override
			{
				using Clock_t = std::chrono::steady_clock;
				if ((!*m_pEnabled && !*m_pMethLab && !m_bApplied) || !Payday3::IsReady() || Clock_t::now() < m_tpNextUpdate || m_bQueued.exchange(true))
					return;
				m_tpNextUpdate = Clock_t::now() + kUpdateInterval;
				if (!Unreal::RunOnGameThread([this] {
						m_bQueued = false;
						Tick();
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
				m_bApplied = m_bKeypadsRestricted || std::ranges::any_of(m_aStations, [](const stStation& rStation) { return rStation.bRestricted; });
			}

			[[nodiscard]] const SDK::UClass* ScriptClass()
			{
				if (!*m_pMethLab)
					return nullptr;
				if (m_pScriptClass && !Payday3::IsLiveObject(m_pScriptClass))
					m_pScriptClass = nullptr;
				const auto tpNow = std::chrono::steady_clock::now();
				if (!m_pScriptClass && tpNow >= m_tpNextClassLookup)
				{
					m_pScriptClass = SDK::AG_Cookie_Scripting_C::StaticClass();
					if (!Payday3::IsLiveObject(m_pScriptClass))
						m_pScriptClass = nullptr;
					m_tpNextClassLookup = tpNow + kClassRetryInterval;
				}
				return m_pScriptClass;
			}

			void Refresh()
			{
				std::vector<SDK::ASBZCookingStation*> aOldStations;
				std::vector<stStation> aKept;
				for (const stStation& rStation : m_aStations)
					aOldStations.push_back(rStation.pStation);
				std::vector<SDK::ASBZKeypadBase*> aKeypads;
				std::vector<SDK::ASBZCookingStation*> aStations;
				SDK::AG_Cookie_Scripting_C* pScript = nullptr;
				const SDK::UClass* pScriptClass = ScriptClass();
				Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
					if (!Payday3::IsAliveActor(pActor))
						return;
					if (pActor->IsA(SDK::ASBZKeypadBase::StaticClass()))
						aKeypads.push_back(static_cast<SDK::ASBZKeypadBase*>(pActor));
					else if (pActor->IsA(SDK::ASBZCookingStation::StaticClass()))
						aStations.push_back(static_cast<SDK::ASBZCookingStation*>(pActor));
					else if (pScriptClass && pActor->IsA(pScriptClass))
						pScript = static_cast<SDK::AG_Cookie_Scripting_C*>(pActor);
				});
				for (SDK::ASBZCookingStation* pStation : aStations)
				{
					const auto itOld = std::ranges::find(m_aStations, pStation, &stStation::pStation);
					aKept.push_back(itOld != m_aStations.end() ? *itOld : stStation{ pStation });
				}
				for (const stStation& rStation : m_aStations)
					if (rStation.bRestricted && std::ranges::find(aStations, rStation.pStation) == aStations.end() && Payday3::IsLiveObject(rStation.pStation))
						SetKeysEnabled(rStation.pStation->IngredientInteractableArray, -1);
				m_aKeypads = std::move(aKeypads);
				m_aStations = std::move(aKept);
				m_pScript = pScript;
				m_tpNextRefresh = std::chrono::steady_clock::now() + kRefreshInterval;
			}

			void UpdateKeypads()
			{
				for (SDK::ASBZKeypadBase* pKeypad : m_aKeypads)
					if (Payday3::IsLiveObject(pKeypad))
						SetKeysEnabled(pKeypad->KeypadInteractableComponentArray, ActiveKey(pKeypad));
				m_bKeypadsRestricted = true;
			}

			void RestoreKeypads()
			{
				if (!m_bKeypadsRestricted)
					return;
				for (SDK::ASBZKeypadBase* pKeypad : m_aKeypads)
					if (Payday3::IsLiveObject(pKeypad))
						SetKeysEnabled(pKeypad->KeypadInteractableComponentArray, -1);
				m_bKeypadsRestricted = false;
			}

			void UpdateStations()
			{
				const bool bRequest = Payday3::IsLiveObject(m_pScript) && m_pScript->IngredientRequestActive;
				const int iRequested = bRequest ? m_pScript->New_Ingredient_Index : -1;
				for (stStation& rStation : m_aStations)
				{
					if (!Payday3::IsLiveObject(rStation.pStation))
						continue;
					SDK::TArray<SDK::USBZInteractableComponent*>& rKeys = rStation.pStation->IngredientInteractableArray;
					const bool bWaiting = rStation.pStation->CurrentState == SDK::ESBZCookingState::WaitingForIngredients;
					const bool bRestrict = bWaiting && iRequested >= 1 && iRequested <= rKeys.Num();
					if (bRestrict)
						SetKeysEnabled(rKeys, iRequested - 1);
					else if (rStation.bRestricted)
						SetKeysEnabled(rKeys, -1);
					rStation.bRestricted = bRestrict;
				}
			}

			void RestoreStations()
			{
				for (stStation& rStation : m_aStations)
					if (rStation.bRestricted && Payday3::IsLiveObject(rStation.pStation))
						SetKeysEnabled(rStation.pStation->IngredientInteractableArray, -1);
				for (stStation& rStation : m_aStations)
					rStation.bRestricted = false;
			}

			bool* m_pEnabled = nullptr;
			bool* m_pMethLab = nullptr;
			bool m_bKeypadsRestricted = false;
			std::atomic<bool> m_bQueued = false;
			std::atomic<bool> m_bApplied = false;
			std::chrono::steady_clock::time_point m_tpNextUpdate;
			std::chrono::steady_clock::time_point m_tpNextRefresh;
			std::vector<SDK::ASBZKeypadBase*> m_aKeypads;
			std::vector<stStation> m_aStations;
			SDK::AG_Cookie_Scripting_C* m_pScript = nullptr;
			SDK::UClass* m_pScriptClass = nullptr;
			std::chrono::steady_clock::time_point m_tpNextClassLookup;
		};

		clKeypadHelperFeature g_clKeypadHelperFeature;
	}
}

#endif
