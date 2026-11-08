#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "Core/Log.hpp"
#include "Core/Profiler.hpp"
#include "Engines/Unreal/Field.hpp"
#include "Engines/Unreal/ProcessEvent.hpp"
#include "Features/Feature.hpp"
#include "Memory/Safety.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace OmegaWare
{
	namespace
	{
		using Clock_t = std::chrono::steady_clock;
		constexpr std::chrono::milliseconds kTickInterval{ 8 };
		constexpr std::chrono::milliseconds kConsumerTimeout{ 1500 };

		struct stClassInfo
		{
			bool bPlayer = false;
			bool bDrone = false;
			bool bVip = false;
			const Payday3::stEnemyInfo* pInfo = nullptr;
		};

		class clWorldFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "world"; }
			[[nodiscard]] bool HasMenu() const override { return false; }

			bool Setup() override
			{
				Unreal::SetGameThreadTick([this] { Features::Guard(*this, "tick", [this] { Tick(); }); }, kTickInterval);
				return true;
			}

			void Destroy() override
			{
				Unreal::SetGameThreadTick({}, kTickInterval);
				std::scoped_lock lckSnapshot(m_mtxSnapshot);
				m_pSnapshot.reset();
			}

			[[nodiscard]] std::shared_ptr<const Payday3::stWorldSnapshot> Current()
			{
				m_iLastRequest.store(Clock_t::now().time_since_epoch().count(), std::memory_order_relaxed);
				std::scoped_lock lckSnapshot(m_mtxSnapshot);
				return m_pSnapshot;
			}

		private:
			[[nodiscard]] bool HasConsumer() const
			{
				const auto tpLast = Clock_t::time_point(Clock_t::duration(m_iLastRequest.load(std::memory_order_relaxed)));
				return Clock_t::now() - tpLast < kConsumerTimeout;
			}

			const stClassInfo& Classify(SDK::AActor* pActor)
			{
				if (const auto itInfo = m_mapClasses.find(pActor->Class); itInfo != m_mapClasses.end())
					return itInfo->second;
				stClassInfo stInfo;
				stInfo.bPlayer = pActor->IsA(SDK::ASBZPlayerCharacter::StaticClass());
				stInfo.bDrone = Payday3::IsDrone(pActor);
				stInfo.pInfo = &Payday3::GetEnemyInfo(Payday3::ResolveEnemyType(pActor));
				stInfo.bVip = !stInfo.bPlayer && stInfo.pInfo->eCategory != Payday3::EEnemyCategory::Cop && Payday3::IsVip(pActor);
				return m_mapClasses.emplace(pActor->Class, stInfo).first->second;
			}

			void Tick()
			{
				if (!HasConsumer())
				{
					std::scoped_lock lckSnapshot(m_mtxSnapshot);
					m_pSnapshot.reset();
					return;
				}
				SDK::UWorld* pWorld = Unreal::GetWorld();
				SDK::USBZActorContainer* pPawns = Payday3::GetAllPawns();
				if (pWorld != m_pWorld)
				{
					m_pWorld = pWorld;
					m_mapClasses.clear();
				}
				auto pSnapshot = std::make_shared<Payday3::stWorldSnapshot>();
				pSnapshot->pWorld = pWorld;
				pSnapshot->pLocalPawn = pWorld ? Unreal::GetLocalPawn() : nullptr;
				if (pPawns)
				{
					SDK::TArray<SDK::UObject*>& aActors = pPawns->Objects;
					pSnapshot->aPawns.reserve(static_cast<std::size_t>(std::max(aActors.Num(), 0)));
					for (int iActor = 0; iActor < aActors.Num(); ++iActor)
					{
						auto* pActor = static_cast<SDK::AActor*>(aActors[iActor]);
						if (!Payday3::IsAliveActor(pActor) || !pActor->Class)
							continue;
						SDK::USkeletalMeshComponent* pMesh = Payday3::GetPawnMesh(pActor);
						if (!pMesh)
							continue;
						const stClassInfo& rClass = Classify(pActor);
						Payday3::stPawnEntry stEntry;
						stEntry.pActor = pActor;
						stEntry.pClass = pActor->Class;
						stEntry.pMesh = pMesh;
						stEntry.pInfo = rClass.pInfo;
						stEntry.bPlayer = rClass.bPlayer;
						stEntry.bDrone = rClass.bDrone;
						stEntry.bVip = rClass.bVip;
						stEntry.bAlive = Payday3::IsPawnAlive(pActor);
						stEntry.vecLocation = Payday3::GetActorLocation(pActor);
						if (rClass.bPlayer)
						{
							stEntry.pPlayerState = OW_FIELD(static_cast<SDK::ASBZPlayerCharacter*>(pActor), SBZPlayerState);
							if (Memory::IsValidObjectPtr(stEntry.pPlayerState))
							{
								stEntry.strName = Payday3::PlayerName(stEntry.pPlayerState);
								stEntry.eModKind = Payday3::GetModUserKind(stEntry.pPlayerState);
							}
							else
								stEntry.pPlayerState = nullptr;
						}
						pSnapshot->aPawns.push_back(std::move(stEntry));
					}
				}
				std::scoped_lock lckSnapshot(m_mtxSnapshot);
				m_pSnapshot = std::move(pSnapshot);
			}

			std::mutex m_mtxSnapshot;
			std::shared_ptr<const Payday3::stWorldSnapshot> m_pSnapshot;
			std::atomic<Clock_t::rep> m_iLastRequest = 0;
			SDK::UWorld* m_pWorld = nullptr;
			std::unordered_map<SDK::UClass*, stClassInfo> m_mapClasses;
		};

		clWorldFeature g_clWorldFeature;
	}

	namespace Payday3
	{
		std::shared_ptr<const stWorldSnapshot> GetWorldSnapshot()
		{
			return g_clWorldFeature.Current();
		}
	}
}

#endif
