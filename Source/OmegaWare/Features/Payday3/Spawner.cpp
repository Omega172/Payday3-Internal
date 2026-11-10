#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "SDK/BP_WoodenCrate_SatellitePart_LowValue_classes.hpp"
#include "SDK/Starbreeze_parameters.hpp"

#include "Config/Config.hpp"
#include "Core/Log.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Engines/Unreal/RpcValidate.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <mutex>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OmegaWare
{
	namespace
	{
		enum ECategory : int
		{
			Entities,
			Loot,
			Bags,
			CategoryCount,
		};

		constexpr std::wstring_view kRequestPrefix = L"#ow-spawn:";
		constexpr std::chrono::milliseconds kCatalogInterval{ 4000 };
		constexpr std::chrono::milliseconds kShootInterval{ 250 };
		constexpr std::chrono::milliseconds kFinishDelay{ 1500 };
		constexpr std::chrono::seconds kSettleTime{ 10 };
		constexpr std::chrono::milliseconds kOverlapRetry{ 500 };
		constexpr int kMaxOverlapTries = 12;
		constexpr std::chrono::milliseconds kReadyPoll{ 250 };
		constexpr std::chrono::milliseconds kFinishCheckDelay{ 2500 };
		constexpr std::chrono::milliseconds kRemoteInterval{ 500 };
		constexpr std::chrono::seconds kRelocateWindow{ 4 };
		constexpr float kTraceDistance = 30000.f;
		constexpr int kMaxCount = 20;

		struct stEntry
		{
			std::string strLabel;
			std::string strKey;
			SDK::FName name;
			SDK::UObject* pObject = nullptr;
			std::int32_t iIndex = -1;
		};

		struct stRequest
		{
			int iCategory = Entities;
			std::string strKey;
			int iCount = 1;
			SDK::FVector vecTarget;
			int iPlayerIndex = 0;
			SDK::AActor* pOrigin = nullptr;
		};

		struct stPendingMove
		{
			std::unordered_set<SDK::AActor*> setBefore;
			SDK::FVector vecTarget;
			std::chrono::steady_clock::time_point tpDeadline;
			int iCount = 1;
			int iMoved = 0;
		};

		[[nodiscard]] std::string Prettify(std::string strName)
		{
			if (strName.starts_with("BP_"))
				strName.erase(0, 3);
			if (strName.ends_with("_C"))
				strName.resize(strName.size() - 2);
			std::ranges::replace(strName, '_', ' ');
			return strName;
		}

		[[nodiscard]] bool IsEditorClassName(const std::string& strName)
		{
			return strName.starts_with("SKEL_") || strName.starts_with("REINST_") || strName.starts_with("HOTRELOAD_") || strName.starts_with("TRASHCLASS_");
		}

		[[nodiscard]] std::string Narrow(std::wstring_view wstrText)
		{
			std::string strResult;
			strResult.reserve(wstrText.size());
			for (const wchar_t wchLetter : wstrText)
				strResult.push_back(wchLetter < 0x80 ? static_cast<char>(wchLetter) : '?');
			return strResult;
		}

		template <typename Number>
		[[nodiscard]] bool ParseNumber(std::string_view strText, Number& rValue)
		{
			const auto [pEnd, eError] = std::from_chars(strText.data(), strText.data() + strText.size(), rValue);
			return eError == std::errc{} && pEnd == strText.data() + strText.size();
		}

		[[nodiscard]] int PlayerIndexOf(const SDK::APlayerState* pState)
		{
			SDK::AGameStateBase* pGameState = Unreal::GetGameState();
			if (!pGameState || !Memory::IsValidObjectPtr(pState))
				return 0;
			SDK::TArray<SDK::APlayerState*>& aPlayers = pGameState->PlayerArray;
			for (int iPlayer = 0; iPlayer < aPlayers.Num(); ++iPlayer)
				if (aPlayers[iPlayer] == pState)
					return iPlayer;
			return 0;
		}

		class clSpawnerFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "spawner"; }

			bool Setup() override
			{
				m_pCategory = &Config::Add("spawner.category", 0);
				m_pItem = &Config::Add("spawner.item", std::string());
				m_pCount = &Config::Add("spawner.count", 1);
				m_pShoot = &Config::Add("spawner.shoot", false);
				m_pAllowClients = &Config::Add("spawner.allow_clients", false);
				m_hMissionEnd = Unreal::OnFunction<SDK::Params::SBZMissionState_Multicast_MissionEnd>("Starbreeze.SBZMissionState.Multicast_MissionEnd",
					[this](SDK::UObject*, SDK::Params::SBZMissionState_Multicast_MissionEnd& rParams, bool&) {
						m_bMissionEndSeen = true;
						LogInfo("spawner: the game started its mission end flow (result {}, outro {})", static_cast<int>(rParams.RequestData.MissionResult), rParams.RequestData.OutroVariation);
					});
				m_hMissionResult = Unreal::OnFunction<SDK::Params::SBZMissionState_Multicast_MissionResult>("Starbreeze.SBZMissionState.Multicast_MissionResult",
					[](SDK::UObject*, SDK::Params::SBZMissionState_Multicast_MissionResult& rParams, bool&) {
						const SDK::FSBZEndMissionResultData& rResult = rParams.InMissionResultData;
						int iCustody = 0;
						for (int iPlayer = 0; iPlayer < rResult.PlayerResultArray.Num(); ++iPlayer)
							if (rResult.PlayerResultArray[iPlayer].bHasBeenInCustody)
								++iCustody;
						LogInfo("spawner: mission result: result {}, bags secured {} of {}, objectives {}, milestones {}, players {}, in custody {}, instant loot entries {}", static_cast<int>(rResult.MissionResult), rResult.SecuredBags.Num(), rResult.BagTotal, rResult.ExperienceObjectiveArray.Num(), rResult.MilestoneArray.Num(), rResult.PlayerResultArray.Num(), iCustody, rResult.InstantLootArray.Num());
						for (int iLoot = 0; iLoot < rResult.InstantLootArray.Num(); ++iLoot)
							LogInfo("spawner: result instant loot {} x {}", rResult.InstantLootArray[iLoot].Name.ToString(), rResult.InstantLootArray[iLoot].Counter);
					});
				return true;
			}

			void Destroy() override
			{
				if (m_hMissionEnd != Unreal::kInvalidHook)
					Unreal::RemoveProcessEvent(m_hMissionEnd);
				if (m_hMissionResult != Unreal::kInvalidHook)
					Unreal::RemoveProcessEvent(m_hMissionResult);
				m_hMissionEnd = Unreal::kInvalidHook;
				m_hMissionResult = Unreal::kInvalidHook;
			}

			void Render() override
			{
				m_bGuiWantsMouse = OmegaGUI::WantsMouse();
			}

			void Run() override
			{
				using Clock_t = std::chrono::steady_clock;
				const auto tpNow = Clock_t::now();
				const bool bClick = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
				const bool bEdge = bClick && !m_bWasClicking;
				m_bWasClicking = bClick;
				if (bEdge && *m_pShoot && !m_bGuiWantsMouse && Payday3::IsReady() && !Payday3::IsAimbotTargeting() && tpNow >= m_tpNextShoot)
				{
					m_tpNextShoot = tpNow + kShootInterval;
					QueueTask([this] { SpawnAtCrosshair(); });
				}
				const bool bMenuOpen = tpNow - m_tpMenuSeen < std::chrono::seconds(1);
				if (!Payday3::IsReady() || m_bTickQueued.exchange(true))
					return;
				const bool bCatalog = bMenuOpen && tpNow >= m_tpNextCatalog;
				if (bCatalog)
					m_tpNextCatalog = tpNow + kCatalogInterval;
				if (!Unreal::RunOnGameThread([this, bCatalog] {
						Features::Guard(*this, "game thread", [this, bCatalog] {
							if (bCatalog)
								BuildCatalog();
							UpdateReadiness(bCatalog);
							ProcessPending();
						});
						m_bTickQueued = false;
					}))
					m_bTickQueued = false;
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				m_tpMenuSeen = std::chrono::steady_clock::now();
				if (!OmegaGUI::BeginSection("spawner.section"_l))
					return;
				OmegaGUI::TextMuted(Role() == ERole::Client ? "spawner.role_client"_t : "spawner.role_host"_t);

				const std::array<std::string_view, CategoryCount> aCategoryLabels = { "spawner.cat_entities"_t, "spawner.cat_loot"_t, "spawner.cat_bags"_t };
				const int iCategory = std::clamp(*m_pCategory, 0, static_cast<int>(CategoryCount) - 1);
				if (OmegaGUI::BeginCombo("spawner.category"_l, aCategoryLabels[iCategory]))
				{
					for (int iOption = 0; iOption < CategoryCount; ++iOption)
						if (OmegaGUI::Selectable(std::format("{}###spawner.cat.{}", aCategoryLabels[iOption], iOption), iOption == iCategory) && iOption != iCategory)
						{
							*m_pCategory = iOption;
							m_pItem->clear();
						}
					OmegaGUI::EndCombo();
				}

				std::vector<stEntry> aEntries;
				{
					std::scoped_lock lckCatalog(m_mtxCatalog);
					aEntries = m_aCatalog[iCategory];
				}
				std::string strPreview(OmegaLocalization::Get("spawner.empty"));
				for (const stEntry& rEntry : aEntries)
					if (rEntry.strKey == *m_pItem)
						strPreview = rEntry.strLabel;
				if (OmegaGUI::BeginCombo("spawner.item"_l, strPreview))
				{
					for (const stEntry& rEntry : aEntries)
						if (OmegaGUI::Selectable(std::format("{}###spawner.item.{}", rEntry.strLabel, rEntry.strKey), rEntry.strKey == *m_pItem))
							*m_pItem = rEntry.strKey;
					OmegaGUI::EndCombo();
				}
				OmegaGUI::SliderInt("spawner.count"_l, m_pCount, 1, kMaxCount);
				OmegaGUI::Checkbox("spawner.shoot"_l, m_pShoot);
				OmegaGUI::ItemTooltip("spawner.shoot_hint"_t);
				if (OmegaGUI::Button("spawner.now"_l))
					QueueTask([this] { SpawnAtCrosshair(); });
				OmegaGUI::Separator();
				if (Role() == ERole::Client)
					OmegaGUI::TextMuted("spawner.finish_host_only"_t);
				else
				{
					const EWait eWait = m_eWait.load();
					if (eWait == EWait::Ready)
					{
						if (OmegaGUI::Button("spawner.finish"_l))
							QueueTask([this] { BeginFinishHeist(); });
						OmegaGUI::ItemTooltip("spawner.finish_hint"_t);
					}
					else
					{
						static constexpr std::array<std::string_view, 8> aKeys = { "", "spawner.wait_world", "spawner.wait_match", "spawner.wait_pawn", "spawner.wait_input", "spawner.wait_scan", "spawner.wait_settle", "spawner.wait_paused" };
						std::string strWait(OmegaLocalization::Get(aKeys[static_cast<std::size_t>(eWait)]));
						if (eWait == EWait::Settling)
							strWait += std::format(" {}", m_iSettleLeft.load());
						OmegaGUI::TextMuted(strWait);
					}
				}
				OmegaGUI::Separator();
				OmegaGUI::Checkbox("spawner.allow_clients"_l, m_pAllowClients);
				OmegaGUI::ItemTooltip("spawner.allow_clients_hint"_t);
				OmegaGUI::EndSection();
			}

			bool OnRemoteRequest(SDK::APlayerState* pSender, std::string_view strPayload)
			{
				if (!Unreal::IsNetHost() || !*m_pAllowClients || !Memory::IsValidObjectPtr(pSender) || Payday3::GetModUserKind(pSender) == Payday3::EModUserKind::None)
					return false;
				std::vector<std::string_view> aParts;
				for (std::size_t uStart = 0; uStart <= strPayload.size();)
				{
					const std::size_t uEnd = strPayload.find(':', uStart);
					aParts.push_back(strPayload.substr(uStart, uEnd == std::string_view::npos ? std::string_view::npos : uEnd - uStart));
					if (uEnd == std::string_view::npos)
						break;
					uStart = uEnd + 1;
				}
				if (aParts.size() != 6)
					return false;
				stRequest stNew;
				double dX = 0., dY = 0., dZ = 0.;
				if (!ParseNumber(aParts[0], stNew.iCategory) || !ParseNumber(aParts[2], stNew.iCount) || !ParseNumber(aParts[3], dX) || !ParseNumber(aParts[4], dY) || !ParseNumber(aParts[5], dZ))
					return false;
				if (stNew.iCategory < 0 || stNew.iCategory >= CategoryCount)
					return false;
				stNew.strKey = std::string(aParts[1]);
				stNew.iCount = std::clamp(stNew.iCount, 1, kMaxCount);
				stNew.vecTarget = SDK::FVector(dX, dY, dZ);
				stNew.iPlayerIndex = PlayerIndexOf(pSender);
				stNew.pOrigin = pSender->PawnPrivate;
				{
					std::scoped_lock lckRemote(m_mtxRemote);
					const auto tpNow = std::chrono::steady_clock::now();
					auto& rNext = m_mapRemoteNext[pSender];
					if (tpNow < rNext)
						return true;
					rNext = tpNow + kRemoteInterval;
				}
				LogInfo("spawner: {} asked for {} x{}", pSender->GetPlayerName().ToString(), stNew.strKey, stNew.iCount);
				QueueTask([this, stNew] { Execute(stNew); });
				return true;
			}

		private:
			enum class ERole : std::uint8_t
			{
				Standalone,
				Host,
				Client,
			};

			static ERole Role()
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				if (!pWorld)
					return ERole::Standalone;
				if (SDK::UKismetSystemLibrary::IsStandalone(pWorld))
					return ERole::Standalone;
				return SDK::UKismetSystemLibrary::IsServer(pWorld) ? ERole::Host : ERole::Client;
			}

			void QueueTask(std::function<void()> fnTask)
			{
				Unreal::RunOnGameThread([this, fnTask = std::move(fnTask)] { Features::Guard(*this, "game thread", fnTask); });
			}

			void BuildCatalog()
			{
				SDK::UClass* pLootBase = SDK::ASBZInstantLoot::StaticClass();
				SDK::UClass* pGeneratorBase = SDK::ASBZSingleBagGenerator::StaticClass();
				SDK::UClass* pPawnData = SDK::USBZPawnSpawnRequestData::StaticClass();
				SDK::UClass* pBagType = SDK::USBZBagType::StaticClass();
				std::array<std::vector<stEntry>, CategoryCount> aCatalog;
				std::unordered_set<std::string> setSeen;
				const SDK::TUObjectArray* pObjects = SDK::UObject::GObjects.GetTypedPtr();
				if (!pObjects)
					return;
				const std::int32_t iCount = pObjects->Num();
				for (std::int32_t iObject = 0; iObject < iCount; ++iObject)
				{
					SDK::UObject* pObject = pObjects->GetByIndex(iObject);
					if (!pObject || !pObject->Class)
						continue;
					const bool bPawnData = pObject->Class == pPawnData;
					const bool bBagType = !bPawnData && pObject->IsA(pBagType);
					const bool bClass = !bPawnData && !bBagType && pObject->HasTypeFlag(SDK::EClassCastFlags::Class);
					if (!bPawnData && !bBagType && !bClass)
						continue;
					if (!Payday3::IsAlive(pObject) || pObject->IsDefaultObject())
						continue;
					const std::string strName = pObject->GetName();
					if (bPawnData)
					{
						if (setSeen.insert("e" + strName).second)
							aCatalog[Entities].push_back({ Prettify(strName), strName, pObject->Name, pObject, pObject->Index });
					}
					else if (bBagType)
					{
						auto* pType = static_cast<SDK::USBZBagType*>(pObject);
						if (pType->WorldItem.Get() && setSeen.insert("b" + strName).second)
							aCatalog[Bags].push_back({ Prettify(strName), strName, pObject->Name, pObject, pObject->Index });
					}
					else if (!IsEditorClassName(strName))
					{
						auto* pClass = static_cast<SDK::UClass*>(pObject);
						if ((pClass->IsSubclassOf(pLootBase) || pClass->IsSubclassOf(pGeneratorBase)) && setSeen.insert("l" + strName).second)
							aCatalog[Loot].push_back({ Prettify(strName), strName, pObject->Name, pObject, pObject->Index });
					}
				}
				for (auto& rList : aCatalog)
					std::ranges::sort(rList, {}, &stEntry::strLabel);
				std::scoped_lock lckCatalog(m_mtxCatalog);
				m_aCatalog = std::move(aCatalog);
			}

			[[nodiscard]] const stEntry* FindEntry(int iCategory, const std::string& strKey, stEntry& rCopy)
			{
				std::scoped_lock lckCatalog(m_mtxCatalog);
				for (const stEntry& rEntry : m_aCatalog[iCategory])
					if (rEntry.strKey == strKey)
					{
						rCopy = rEntry;
						return &rCopy;
					}
				return nullptr;
			}

			static SDK::FTransform MakeTransform(const SDK::FVector& vecLocation, double dYawDegrees)
			{
				const double dHalf = dYawDegrees * (std::numbers::pi / 180.0) * .5;
				SDK::FTransform stTransform{};
				stTransform.Rotation.Z = std::sin(dHalf);
				stTransform.Rotation.W = std::cos(dHalf);
				stTransform.Translation = vecLocation;
				stTransform.Scale3D = SDK::FVector(1.0, 1.0, 1.0);
				return stTransform;
			}

			void SpawnAtCrosshair()
			{
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				SDK::UWorld* pWorld = Unreal::GetWorld();
				if (!Payday3::IsAlive(pController) || !pWorld || !Payday3::IsAlive(pController->PlayerCameraManager) || !Payday3::IsAliveActor(pController->AcknowledgedPawn))
					return;
				const int iCategory = std::clamp(*m_pCategory, 0, static_cast<int>(CategoryCount) - 1);
				if (m_pItem->empty())
					return;
				const SDK::FMinimalViewInfo& rView = pController->PlayerCameraManager->CameraCachePrivate.POV;
				const double dPitch = rView.Rotation.Pitch * (std::numbers::pi / 180.0);
				const double dYaw = rView.Rotation.Yaw * (std::numbers::pi / 180.0);
				const SDK::FVector vecForward(std::cos(dPitch) * std::cos(dYaw), std::cos(dPitch) * std::sin(dYaw), std::sin(dPitch));
				const SDK::FVector vecEnd = rView.Location + vecForward * static_cast<double>(kTraceDistance);
				SDK::FHitResult stHit{};
				SDK::TArray<SDK::AActor*> aIgnore;
				SDK::FVector vecTarget = rView.Location + vecForward * 1500.0;
				if (SDK::UKismetSystemLibrary::LineTraceSingle(pController->AcknowledgedPawn, rView.Location, vecEnd, SDK::ETraceTypeQuery::TraceTypeQuery1, false, aIgnore, SDK::EDrawDebugTrace::None, &stHit, true, SDK::FLinearColor{}, SDK::FLinearColor{}, 0.f))
					vecTarget = SDK::FVector(stHit.ImpactPoint.X, stHit.ImpactPoint.Y, stHit.ImpactPoint.Z);

				stRequest stNew;
				stNew.iCategory = iCategory;
				stNew.strKey = *m_pItem;
				stNew.iCount = std::clamp(*m_pCount, 1, kMaxCount);
				stNew.vecTarget = vecTarget;
				stNew.pOrigin = pController->AcknowledgedPawn;
				stNew.iPlayerIndex = PlayerIndexOf(pController->PlayerState);
				if (Role() == ERole::Client)
				{
					if (Payday3::ModUserCount() == 0)
					{
						LogWarning("spawner: no OmegaWare user was detected in this lobby, the request was not sent");
						return;
					}
					if (!Payday3::SendModChat(std::format(L"{}{}:{}:{}:{:.0f}:{:.0f}:{:.0f}", kRequestPrefix, stNew.iCategory, std::wstring(stNew.strKey.begin(), stNew.strKey.end()), stNew.iCount, vecTarget.X, vecTarget.Y, vecTarget.Z)))
						LogWarning("spawner: could not send the request to the host");
					return;
				}
				Execute(stNew);
			}

			void Execute(const stRequest& rRequest)
			{
				stEntry stCopy;
				const stEntry* pEntry = FindEntry(rRequest.iCategory, rRequest.strKey, stCopy);
				if (!pEntry || !Payday3::IsSameObject(pEntry->pObject, pEntry->iIndex))
				{
					LogWarning("spawner: {} is not available in this level", rRequest.strKey);
					return;
				}
				switch (rRequest.iCategory)
				{
				case Entities:
					SpawnEntities(rRequest, *pEntry);
					break;
				case Loot:
					SpawnLoot(rRequest, *pEntry);
					break;
				default:
					SpawnBags(rRequest, *pEntry);
					break;
				}
			}

			static SDK::UCheatManager* EnsureCheatManager(SDK::APlayerController* pController)
			{
				if (Payday3::IsAlive(pController->CheatManager))
					return pController->CheatManager;
				SDK::UObject* pObject = SDK::UGameplayStatics::SpawnObject(SDK::USBZCheatManager::StaticClass(), pController);
				if (!Payday3::IsAlive(pObject) || !pObject->IsA(SDK::UCheatManager::StaticClass()))
					return nullptr;
				pController->CheatManager = static_cast<SDK::UCheatManager*>(pObject);
				LogInfo("spawner: created a cheat manager for the local player");
				return pController->CheatManager;
			}

			void SpawnEntities(const stRequest& rRequest, const stEntry& rEntry)
			{
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				SDK::UCheatManager* pCheat = Payday3::IsAlive(pController) ? EnsureCheatManager(pController) : nullptr;
				if (!pCheat || !pCheat->IsA(SDK::USBZCheatManager::StaticClass()))
				{
					LogWarning("spawner: the game's cheat manager is not available, enemies cannot be spawned");
					return;
				}
				stPendingMove stMove;
				if (SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns())
				{
					SDK::TArray<SDK::UObject*>& aActors = pPawns->Objects;
					for (int iActor = 0; iActor < aActors.Num(); ++iActor)
						stMove.setBefore.insert(static_cast<SDK::AActor*>(aActors[iActor]));
				}
				stMove.vecTarget = rRequest.vecTarget;
				stMove.iCount = rRequest.iCount;
				stMove.tpDeadline = std::chrono::steady_clock::now() + kRelocateWindow;
				const SDK::FVector vecOrigin = Payday3::IsAliveActor(rRequest.pOrigin) ? Payday3::GetActorLocation(rRequest.pOrigin) : rRequest.vecTarget;
				const float flDistance = std::clamp(static_cast<float>((rRequest.vecTarget - vecOrigin).Magnitude()), 250.f, 6000.f);
				static_cast<SDK::USBZCheatManager*>(pCheat)->Spawn(rEntry.name, rRequest.iCount, flDistance, rRequest.iPlayerIndex, 0.f, SDK::FName{});
				m_aPending.push_back(std::move(stMove));
				LogInfo("spawner: asked the game to spawn {} x{} for player {}", rEntry.strKey, rRequest.iCount, rRequest.iPlayerIndex);
			}

			void SpawnLoot(const stRequest& rRequest, const stEntry& rEntry)
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				auto* pClass = static_cast<SDK::UClass*>(rEntry.pObject);
				int iSpawned = 0;
				for (int iItem = 0; iItem < rRequest.iCount; ++iItem)
				{
					const SDK::FVector vecAt = Offset(rRequest.vecTarget, iItem, 35.0);
					const SDK::FTransform stTransform = MakeTransform(vecAt, iItem * 37.0);
					SDK::AActor* pActor = SDK::UGameplayStatics::BeginDeferredActorSpawnFromClass(pWorld, pClass, stTransform, SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr, SDK::ESpawnActorScaleMethod::OverrideRootScale);
					if (!Payday3::IsAliveActor(pActor))
						continue;
					SDK::UGameplayStatics::FinishSpawningActor(pActor, stTransform, SDK::ESpawnActorScaleMethod::OverrideRootScale);
					++iSpawned;
				}
				LogInfo("spawner: spawned {} x{} of {} at the crosshair", rEntry.strKey, iSpawned, rRequest.iCount);
			}

			void SpawnBags(const stRequest& rRequest, const stEntry& rEntry)
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::USBZBagManager* pManager = SDK::USBZBagManager::Get(pWorld);
				auto* pType = static_cast<SDK::USBZBagType*>(rEntry.pObject);
				SDK::UClass* pClass = pType->WorldItem.Get();
				if (!Payday3::IsAlive(pManager) || !pClass)
				{
					LogWarning("spawner: the bag manager or the bag class for {} is not available", rEntry.strKey);
					return;
				}
				int iSpawned = 0;
				for (int iBag = 0; iBag < rRequest.iCount; ++iBag)
				{
					const SDK::FSBZBagHandle stHandle = pManager->CreateBag(pType);
					const SDK::FVector vecAt = Offset(rRequest.vecTarget, iBag, 60.0);
					const SDK::FTransform stTransform = MakeTransform(vecAt + SDK::FVector(0.0, 0.0, 40.0), iBag * 53.0);
					SDK::AActor* pActor = SDK::UGameplayStatics::BeginDeferredActorSpawnFromClass(pWorld, pClass, stTransform, SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr, SDK::ESpawnActorScaleMethod::OverrideRootScale);
					if (!Payday3::IsAliveActor(pActor) || !pActor->IsA(SDK::ASBZBagItem::StaticClass()))
						continue;
					auto* pBag = static_cast<SDK::ASBZBagItem*>(pActor);
					pBag->Bag = stHandle;
					pBag->BagId = stHandle.Id;
					SDK::UGameplayStatics::FinishSpawningActor(pActor, stTransform, SDK::ESpawnActorScaleMethod::OverrideRootScale);
					pManager->ClaimBag(stHandle, pActor);
					++iSpawned;
				}
				LogInfo("spawner: spawned {} x{} of {} at the crosshair", rEntry.strKey, iSpawned, rRequest.iCount);
			}

			static SDK::FVector Offset(const SDK::FVector& vecCenter, int iIndex, double dRadius)
			{
				if (iIndex == 0)
					return vecCenter + SDK::FVector(0.0, 0.0, 20.0);
				const double dAngle = iIndex * 2.399963;
				const double dReach = dRadius * std::sqrt(static_cast<double>(iIndex));
				return vecCenter + SDK::FVector(std::cos(dAngle) * dReach, std::sin(dAngle) * dReach, 20.0);
			}

			enum class EWait : int
			{
				Ready,
				NoWorld,
				MatchNotStarted,
				NoPawn,
				InputLocked,
				NoScan,
				Settling,
				Paused,
			};

			void UpdateReadiness(bool bScan)
			{
				const auto tpNow = std::chrono::steady_clock::now();
				if (tpNow < m_tpNextReadyPoll && !bScan)
					return;
				m_tpNextReadyPoll = tpNow + kReadyPoll;
				SDK::UWorld* pWorld = Unreal::GetWorld();
				if (pWorld != m_pReadyWorld)
				{
					m_pReadyWorld = pWorld;
					m_bHasEscape = false;
					m_bHasObjectives = false;
					m_bScanned = false;
					m_bPlayable = false;
				}
				EWait eWait = CheckPlayable();
				if (eWait == EWait::Ready)
				{
					if (!m_bPlayable)
					{
						m_bPlayable = true;
						m_tpPlayable = tpNow;
					}
					if (bScan || !m_bScanned)
						ScanForFinish();
					if (!m_bHasEscape || !m_bHasObjectives)
						eWait = EWait::NoScan;
					else if (tpNow - m_tpPlayable < kSettleTime)
					{
						eWait = EWait::Settling;
						m_iSettleLeft = static_cast<int>(std::chrono::ceil<std::chrono::seconds>(kSettleTime - (tpNow - m_tpPlayable)).count());
					}
				}
				else if (eWait != EWait::Paused)
					m_bPlayable = false;
				if (eWait != m_eWait.load())
					LogInfo("spawner: finish heist readiness is now {}", static_cast<int>(eWait));
				m_eWait = eWait;
			}

			[[nodiscard]] static EWait CheckPlayable()
			{
				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::AGameStateBase* pGameState = Unreal::GetGameState();
				if (!pWorld || !Payday3::IsAlive(pGameState))
					return EWait::NoWorld;
				if (SDK::UGameplayStatics::IsGamePaused(pWorld))
					return EWait::Paused;
				if (!pGameState->HasMatchStarted())
					return EWait::MatchNotStarted;
				SDK::APawn* pPawn = Unreal::GetLocalPawn();
				if (!Payday3::IsAliveActor(pPawn) || !pPawn->IsA(SDK::ASBZPlayerCharacter::StaticClass()))
					return EWait::NoPawn;
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				if (!Payday3::IsAlive(pController) || pController->IsMoveInputIgnored())
					return EWait::InputLocked;
				return EWait::Ready;
			}

			void ScanForFinish()
			{
				bool bEscape = false;
				bool bObjectives = false;
				Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
					if (bEscape && bObjectives)
						return;
					if (!Payday3::IsAliveActor(pActor))
						return;
					if (pActor->IsA(SDK::ASBZPlayerEscapeVolume::StaticClass()))
						bEscape = true;
					else if (pActor->IsA(SDK::ASBZObjective::StaticClass()))
						bObjectives = true;
				});
				m_bHasEscape = bEscape;
				m_bHasObjectives = bObjectives;
				m_bScanned = true;
			}

			void BeginFinishHeist()
			{
				if (!Unreal::IsNetHost())
				{
					LogWarning("spawner: only the host can finish the heist");
					return;
				}
				UpdateReadiness(true);
				if (m_eWait.load() != EWait::Ready)
				{
					LogWarning("spawner: the mission is not ready to be finished yet (state {})", static_cast<int>(m_eWait.load()));
					return;
				}
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				SDK::UCheatManager* pCheat = Payday3::IsAlive(pController) ? EnsureCheatManager(pController) : nullptr;
				if (!pCheat || !pCheat->IsA(SDK::USBZCheatManager::StaticClass()))
				{
					LogWarning("spawner: the game's cheat manager is not available, the heist cannot be finished");
					return;
				}
				auto* pSbzCheat = static_cast<SDK::USBZCheatManager*>(pCheat);
				SDK::AGameStateBase* pGameState = Unreal::GetGameState();
				m_iFinishPlayers = Payday3::IsAlive(pGameState) ? std::max(pGameState->PlayerArray.Num(), 1) : 1;
				pSbzCheat->ListenToRewardLogs();
				const int iLooseBefore = CountUnlootedInstant();
				pSbzCheat->GrabInstantLoot();
				LogInfo("spawner: loose loot before the grab all request {}, after {}", iLooseBefore, CountUnlootedInstant());
				GrabRemainingInstantLoot();
				BankGeneratedBags(pSbzCheat);
				SecureManagerBags(pSbzCheat);
				pSbzCheat->SecureAllAvailableLoot(true, true);
				LogManagerBags("after the secure all request");
				m_iOverlapTries = 0;
				s_iVolumeIndex = 0;
				if (SDK::UWorld* pWorld = Unreal::GetWorld())
					LogInfo("spawner: finishing the heist on {}", pWorld->GetName());
				m_flEscapeSeconds = MovePlayersToEscape();
				m_tpFinishAt = std::chrono::steady_clock::now() + kFinishDelay;
				m_bFinishPending = true;
				LogInfo("spawner: secured all available loot, finishing the heist in {} ms for {} players", kFinishDelay.count(), m_iFinishPlayers);
			}

			static float MovePlayersToEscape()
			{
				SDK::ASBZPlayerEscapeVolume* pVolume = FindEscapeVolume();
				SDK::AGameStateBase* pGameState = Unreal::GetGameState();
				if (!pVolume || !Payday3::IsAlive(pGameState))
				{
					LogWarning("spawner: no escape volume was found, players are not moved so they may count as in custody");
					return 0.f;
				}
				pVolume->SetVolumeEnabled(true);
				const SDK::FVector vecCenter = pVolume->K2_GetActorLocation();
				SDK::APawn* pLocalPawn = Unreal::GetLocalPawn();
				int iSlot = 0;
				SDK::TArray<SDK::APlayerState*>& aPlayers = pGameState->PlayerArray;
				for (int iPlayer = 0; iPlayer < aPlayers.Num(); ++iPlayer)
				{
					SDK::APlayerState* pState = aPlayers[iPlayer];
					SDK::APawn* pPawn = Payday3::IsAlive(pState) ? pState->PawnPrivate : nullptr;
					if (!Payday3::IsAliveActor(pPawn) || !pPawn->IsA(SDK::ASBZPlayerCharacter::StaticClass()))
						continue;
					const SDK::FVector vecAt = Offset(vecCenter, iSlot++, 80.0) + SDK::FVector(0.0, 0.0, 40.0);
					pPawn->K2_SetActorLocation(vecAt, false, nullptr, true);
					if (pPawn != pLocalPawn)
						static_cast<SDK::ASBZPlayerCharacter*>(pPawn)->Client_Teleport(vecAt, 0.f);
				}
				LogInfo("spawner: moved {} players into the escape volume, which needs {:.0f} s", iSlot, pVolume->EscapeTimeDuration);
				return pVolume->EscapeTimeDuration;
			}

			static int CountUnlootedInstant()
			{
				int iCount = 0;
				Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
					if (Payday3::IsAliveActor(pActor) && pActor->IsA(SDK::ASBZInstantLoot::StaticClass()) && !pActor->bHidden && !static_cast<SDK::ASBZInstantLoot*>(pActor)->bIsLooted)
						++iCount;
				});
				return iCount;
			}

			static void GrabRemainingInstantLoot()
			{
				SDK::ASBZPlayerCharacter* pCharacter = Payday3::GetLocalCharacter();
				SDK::USBZPlayerInteractorComponent* pInteractor = pCharacter ? pCharacter->Interactor : nullptr;
				if (!Payday3::IsAlive(pInteractor))
				{
					LogWarning("spawner: no interactor was found, loose loot is not grabbed by hand");
					return;
				}
				std::vector<SDK::USBZInteractableComponent*> aLoot;
				Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
					if (!Payday3::IsAliveActor(pActor) || !pActor->IsA(SDK::ASBZInstantLoot::StaticClass()) || pActor->bHidden)
						return;
					auto* pLoot = static_cast<SDK::ASBZInstantLoot*>(pActor);
					if (!pLoot->bIsLooted && Payday3::IsAlive(pLoot->Interactable) && !pLoot->Interactable->bIsHidden)
						aLoot.push_back(pLoot->Interactable);
				});
				for (SDK::USBZInteractableComponent* pInteractable : aLoot)
				{
					const std::int32_t iId = pInteractor->InteractId + 1;
					pInteractor->InteractId = iId;
					pInteractor->Server_StartInteraction(pInteractable, iId, 0);
					pInteractor->Server_CompleteInteraction(pInteractable, iId);
				}
				LogInfo("spawner: grabbed {} loose loot items by hand, {} left", aLoot.size(), CountUnlootedInstant());
			}

			static void LogManagerBags(std::string_view strWhen)
			{
				SDK::USBZBagManager* pManager = SDK::USBZBagManager::Get(Unreal::GetWorld());
				if (!Payday3::IsAlive(pManager))
				{
					LogWarning("spawner: the bag manager was not found {}", strWhen);
					return;
				}
				int iSecured = 0;
				const SDK::TArray<SDK::FSBZBagPersistentData>& aBags = pManager->Bags;
				for (int iBag = 0; iBag < aBags.Num(); ++iBag)
					if (aBags[iBag].Secured)
						++iSecured;
				LogInfo("spawner: the bag manager holds {} bags and {} are secured {}", aBags.Num(), iSecured, strWhen);
			}

			static std::unordered_map<SDK::USBZBagType*, SDK::USBZBagType*> BuildWashMap()
			{
				std::unordered_map<SDK::USBZBagType*, SDK::USBZBagType*> mapWash;
				Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
					if (!Payday3::IsAliveActor(pActor) || !pActor->IsA(SDK::ASBZLootProcessorBase::StaticClass()))
						return;
					const SDK::TMap<SDK::USBZBagType*, SDK::USBZBagType*>& rTypes = static_cast<SDK::ASBZLootProcessorBase*>(pActor)->BagTypeMap;
					for (int iType = 0; iType < rTypes.NumAllocated(); ++iType)
					{
						if (!rTypes.IsValidIndex(iType))
							continue;
						SDK::USBZBagType* pFrom = rTypes[iType].Key();
						SDK::USBZBagType* pTo = rTypes[iType].Value();
						if (Payday3::IsAlive(pFrom) && Payday3::IsAlive(pTo) && pFrom != pTo)
							mapWash[pFrom] = pTo;
					}
				});
				return mapWash;
			}

			static void SecureManagerBags(SDK::USBZCheatManager* pCheat)
			{
				SDK::USBZBagManager* pManager = SDK::USBZBagManager::Get(Unreal::GetWorld());
				if (!Payday3::IsAlive(pManager))
				{
					LogWarning("spawner: the bag manager was not found, bags are not secured directly");
					return;
				}
				LogManagerBags("before securing");
				const std::unordered_map<SDK::USBZBagType*, SDK::USBZBagType*> mapWash = BuildWashMap();
				std::unordered_map<std::string, std::pair<SDK::FName, int>> mapWashed;
				std::vector<std::int32_t> aWashIds;
				for (int iBag = 0; iBag < pManager->Bags.Num(); ++iBag)
				{
					const SDK::FSBZBagPersistentData& rBag = pManager->Bags[iBag];
					if (rBag.Secured)
						continue;
					const auto itWash = mapWash.find(rBag.Handle.BagType);
					if (itWash == mapWash.end())
						continue;
					aWashIds.push_back(rBag.Handle.Id);
					auto& rEntry = mapWashed[itWash->second->GetName()];
					rEntry.first = itWash->second->Name;
					++rEntry.second;
				}
				for (const std::int32_t iId : aWashIds)
					pManager->Multicast_RemoveBag(iId);
				if (Payday3::IsAlive(pCheat))
					for (const auto& [strName, rEntry] : mapWashed)
				{
					pCheat->SecureBag(rEntry.second, rEntry.first);
					LogInfo("spawner: washed {} unsecured bags into {} x {} through the loot processor map", rEntry.second, rEntry.second, strName);
				}
				std::vector<SDK::FSBZBagHandle> aHandles;
				const SDK::TArray<SDK::FSBZBagPersistentData>& aBags = pManager->Bags;
				for (int iBag = 0; iBag < aBags.Num(); ++iBag)
					if (!aBags[iBag].Secured)
						aHandles.push_back(aBags[iBag].Handle);
				for (const SDK::FSBZBagHandle& rHandle : aHandles)
					pManager->SecureBag(rHandle, true, true);
				LogInfo("spawner: asked the bag manager to secure {} bags directly", aHandles.size());
				LogManagerBags("after securing");
			}

			static bool IsUnused(SDK::USBZInteractableComponent* pInteractable)
			{
				return Payday3::IsAlive(pInteractable) && pInteractable->bInteractionEnabled && !pInteractable->bIsHidden;
			}

			static void BankGeneratedBags(SDK::USBZCheatManager* pCheat)
			{
				std::unordered_map<std::string, std::pair<SDK::FName, int>> mapBags;
				const std::unordered_map<SDK::USBZBagType*, SDK::USBZBagType*> mapWash = BuildWashMap();
				std::unordered_set<SDK::AActor*> setSeen;
				int iInsideBags = 0;
				auto fnAdd = [&](SDK::USBZBagType* pType, int iCount) {
					if (!Payday3::IsAlive(pType) || iCount <= 0)
						return;
					if (const auto itWash = mapWash.find(pType); itWash != mapWash.end())
						pType = itWash->second;
					auto& rEntry = mapBags[pType->GetName()];
					rEntry.first = pType->Name;
					rEntry.second += iCount;
				};
				Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
					if (!Payday3::IsAliveActor(pActor))
						return;
					if (pActor->IsA(SDK::ASBZSingleBagGenerator::StaticClass()))
					{
						auto* pGenerator = static_cast<SDK::ASBZSingleBagGenerator*>(pActor);
						if (!pGenerator->bHidden)
						{
							setSeen.insert(pActor);
							fnAdd(pGenerator->BagType, 1);
						}
					}
					else if (pActor->IsA(SDK::ASBZMultiBagGenerator::StaticClass()))
					{
						auto* pGenerator = static_cast<SDK::ASBZMultiBagGenerator*>(pActor);
						if (!pGenerator->bHidden && !pGenerator->bIsInfinite)
						{
							setSeen.insert(pActor);
							fnAdd(pGenerator->BagType, pGenerator->NumberOfBags);
						}
					}
					else if (pActor->IsA(SDK::ASBZBagSpawner::StaticClass()))
						fnAdd(static_cast<SDK::ASBZBagSpawner*>(pActor)->BagTypeToSpawn, 1);
				});
				SDK::UClass* pSatelliteCrate = SDK::ABP_WoodenCrate_SatellitePart_LowValue_C::StaticClass();
				auto fnInside = [&](SDK::AActor* pInside) {
					if (!Payday3::IsAliveActor(pInside) || setSeen.contains(pInside))
						return;
					if (pInside->IsA(SDK::ASBZSingleBagGenerator::StaticClass()))
					{
						setSeen.insert(pInside);
						fnAdd(static_cast<SDK::ASBZSingleBagGenerator*>(pInside)->BagType, 1);
						iInsideBags++;
					}
					else if (pInside->IsA(SDK::ASBZMultiBagGenerator::StaticClass()))
					{
						auto* pMulti = static_cast<SDK::ASBZMultiBagGenerator*>(pInside);
						if (pMulti->bIsInfinite)
							return;
						setSeen.insert(pInside);
						fnAdd(pMulti->BagType, pMulti->NumberOfBags);
						iInsideBags += pMulti->NumberOfBags;
					}
				};
				Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
					if (!Payday3::IsAliveActor(pActor))
						return;
					if (pActor->IsA(SDK::ASBZItemContainer::StaticClass()))
					{
						auto* pContainer = static_cast<SDK::ASBZItemContainer*>(pActor);
						if (pContainer->bHasItem && Payday3::IsAlive(pContainer->InteractableComponent) && pContainer->InteractableComponent->bInteractionEnabled && !pContainer->InteractableComponent->bIsHidden)
							fnInside(pContainer->HiddenItem);
					}
					else if (pSatelliteCrate && pActor->IsA(pSatelliteCrate))
					{
						auto* pCrate = static_cast<SDK::ABP_WoodenCrate_SatellitePart_LowValue_C*>(pActor);
						if (Payday3::IsAlive(pCrate->SBZInteractable) && pCrate->SBZInteractable->bInteractionEnabled && !pCrate->SBZInteractable->bIsHidden)
							for (int iInside = 0; iInside < pCrate->actorsInside.Num(); ++iInside)
								fnInside(pCrate->actorsInside[iInside]);
					}
				});
				if (iInsideBags > 0)
					LogInfo("spawner: counted {} bags that were still inside unopened containers and crates", iInsideBags);
				int iBanked = 0;
				for (const auto& [strName, rEntry] : mapBags)
				{
					pCheat->SecureBag(rEntry.second, rEntry.first);
					iBanked += rEntry.second;
					LogInfo("spawner: banked {} x {} straight into the secured loot", rEntry.second, strName);
				}
				LogInfo("spawner: banked {} bags from {} bag types that had not been picked up yet", iBanked, mapBags.size());
			}

			static SDK::ASBZPlayerEscapeVolume* FindEscapeVolume(int* piCount = nullptr)
			{
				std::vector<SDK::ASBZPlayerEscapeVolume*> aVolumes;
				Payday3::ForEachLevelActor(Unreal::GetWorld(), [&](SDK::AActor* pActor) {
					if (Payday3::IsAliveActor(pActor) && pActor->IsA(SDK::ASBZPlayerEscapeVolume::StaticClass()))
						aVolumes.push_back(static_cast<SDK::ASBZPlayerEscapeVolume*>(pActor));
				});
				if (piCount)
					*piCount = static_cast<int>(aVolumes.size());
				return aVolumes.empty() ? nullptr : aVolumes[static_cast<std::size_t>(s_iVolumeIndex) % aVolumes.size()];
			}

			static void LogEscapeStatus(std::string_view strWhen)
			{
				int iCount = 0;
				SDK::ASBZPlayerEscapeVolume* pVolume = FindEscapeVolume(&iCount);
				if (!pVolume)
				{
					LogWarning("spawner: no escape volume {}", strWhen);
					return;
				}
				const SDK::FVector vecAt = pVolume->K2_GetActorLocation();
				LogInfo("spawner: escape volume {} ({} of {}, {} at {:.0f} {:.0f} {:.0f}): {} players overlapping, all players inside {}, hidden {}", strWhen, s_iVolumeIndex % iCount + 1, iCount, pVolume->GetName(), vecAt.X, vecAt.Y, vecAt.Z, pVolume->GetCurrentNumOverlappingPlayers(), pVolume->AreAllPlayerPawnsInside(), static_cast<bool>(pVolume->bHidden));
			}

			void FinishHeist()
			{
				LogEscapeStatus("when the finish is requested");
				if (SDK::ASBZPlayerEscapeVolume* pVolume = FindEscapeVolume(); pVolume && (pVolume->GetCurrentNumOverlappingPlayers() < m_iFinishPlayers || !pVolume->AreAllPlayerPawnsInside()) && m_iOverlapTries < kMaxOverlapTries)
				{
					++m_iOverlapTries;
					++s_iVolumeIndex;
					MovePlayersToEscape();
					m_tpFinishAt = std::chrono::steady_clock::now() + kOverlapRetry;
					m_bFinishPending = true;
					return;
				}
				SDK::APlayerController* pController = Unreal::GetPlayerController();
				SDK::UCheatManager* pCheat = Payday3::IsAlive(pController) ? pController->CheatManager : nullptr;
				if (!Payday3::IsAlive(pCheat) || !pCheat->IsA(SDK::USBZCheatManager::StaticClass()))
					return;
				m_bMissionEndSeen = false;
				static_cast<SDK::USBZCheatManager*>(pCheat)->RequestMissionSuccess(m_iFinishPlayers);
				LogInfo("spawner: asked the game to end the mission as a success with {} players in the escape volume", m_iFinishPlayers);
				m_bFinishCheck = true;
				m_tpFinishCheck = std::chrono::steady_clock::now() + std::max(kFinishCheckDelay, std::chrono::milliseconds(static_cast<std::int64_t>((m_flEscapeSeconds + 3.f) * 1000.f)));
			}

			void FinishHeistFallback()
			{
				if (m_bMissionEndSeen)
					return;
				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::USBZGameStateMachine* pMachine = pWorld ? SDK::USBZGameStateMachineFunctionLibrary::GetGameStateMachine(pWorld) : nullptr;
				if (!Payday3::IsAlive(pMachine))
				{
					LogWarning("spawner: the game ignored the cheat manager request and no game state machine was found");
					return;
				}
				LogWarning("spawner: the game ignored the cheat manager request, calling its game state machine {} directly", static_cast<const void*>(pMachine));
				LogEscapeStatus("before the fallback");
				if (SDK::ASBZPlayerEscapeVolume* pVolume = FindEscapeVolume())
				{
					const bool bInside = pVolume->GetCurrentNumOverlappingPlayers() >= m_iFinishPlayers && pVolume->AreAllPlayerPawnsInside();
					if (!bInside)
					{
						++s_iVolumeIndex;
						MovePlayersToEscape();
					}
					if (!bInside && m_iOverlapTries < kMaxOverlapTries)
					{
						++m_iOverlapTries;
						m_bFinishCheck = true;
						m_tpFinishCheck = std::chrono::steady_clock::now() + kOverlapRetry;
						return;
					}
				}
				SDK::FSBZEndMissionRequestData stRequest{};
				stRequest.MissionResult = SDK::ESBZEndMissionResult::Success;
				stRequest.OutroVariation = 0;
				pMachine->RequestMissionEnd(stRequest);
				LogInfo("spawner: asked the game state machine to end the mission as a success");
			}

			void ProcessPending()
			{
				if (m_bFinishPending && std::chrono::steady_clock::now() >= m_tpFinishAt)
				{
					m_bFinishPending = false;
					FinishHeist();
				}
				if (m_bFinishCheck && std::chrono::steady_clock::now() >= m_tpFinishCheck)
				{
					m_bFinishCheck = false;
					FinishHeistFallback();
				}
				if (m_aPending.empty())
					return;
				SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns();
				const auto tpNow = std::chrono::steady_clock::now();
				for (stPendingMove& rMove : m_aPending)
				{
					if (!pPawns)
						break;
					SDK::TArray<SDK::UObject*>& aActors = pPawns->Objects;
					for (int iActor = 0; iActor < aActors.Num(); ++iActor)
					{
						auto* pActor = static_cast<SDK::AActor*>(aActors[iActor]);
						if (!Payday3::IsAliveActor(pActor) || rMove.setBefore.contains(pActor))
							continue;
						rMove.setBefore.insert(pActor);
						const SDK::FVector vecAt = Offset(rMove.vecTarget, rMove.iMoved, 90.0);
						pActor->K2_SetActorLocation(vecAt + SDK::FVector(0.0, 0.0, 90.0), false, nullptr, true);
						++rMove.iMoved;
					}
				}
				std::erase_if(m_aPending, [&](const stPendingMove& rMove) { return tpNow >= rMove.tpDeadline || rMove.iMoved >= rMove.iCount; });
			}

			int* m_pCategory = nullptr;
			std::string* m_pItem = nullptr;
			int* m_pCount = nullptr;
			bool* m_pShoot = nullptr;
			bool* m_pAllowClients = nullptr;
			std::mutex m_mtxCatalog;
			std::array<std::vector<stEntry>, CategoryCount> m_aCatalog;
			std::mutex m_mtxRemote;
			std::unordered_map<SDK::APlayerState*, std::chrono::steady_clock::time_point> m_mapRemoteNext;
			std::vector<stPendingMove> m_aPending;
			std::atomic<bool> m_bGuiWantsMouse = false;
			std::atomic<bool> m_bTickQueued = false;
			std::atomic<EWait> m_eWait = EWait::NoWorld;
			std::atomic<int> m_iSettleLeft = 0;
			SDK::UWorld* m_pReadyWorld = nullptr;
			bool m_bPlayable = false;
			bool m_bHasEscape = false;
			bool m_bHasObjectives = false;
			bool m_bScanned = false;
			std::chrono::steady_clock::time_point m_tpPlayable;
			std::chrono::steady_clock::time_point m_tpNextReadyPoll;
			bool m_bFinishPending = false;
			float m_flEscapeSeconds = 0.f;
			int m_iOverlapTries = 0;
			static inline int s_iVolumeIndex = 0;
			bool m_bFinishCheck = false;
			std::atomic<bool> m_bMissionEndSeen = false;
			std::chrono::steady_clock::time_point m_tpFinishCheck;
			Unreal::HookHandle m_hMissionEnd = Unreal::kInvalidHook;
			Unreal::HookHandle m_hMissionResult = Unreal::kInvalidHook;
			int m_iFinishPlayers = 1;
			std::chrono::steady_clock::time_point m_tpFinishAt;
			bool m_bWasClicking = false;
			std::chrono::steady_clock::time_point m_tpNextShoot;
			std::chrono::steady_clock::time_point m_tpNextCatalog;
			std::chrono::steady_clock::time_point m_tpMenuSeen;
		};

		clSpawnerFeature g_clSpawnerFeature;
	}

	namespace Payday3
	{
		bool HandleSpawnMessage(SDK::APlayerState* pSender, const std::wstring& wstrText)
		{
			if (!wstrText.starts_with(kRequestPrefix))
				return false;
			g_clSpawnerFeature.OnRemoteRequest(pSender, Narrow(std::wstring_view(wstrText).substr(kRequestPrefix.size())));
			return true;
		}
	}
}

#endif
