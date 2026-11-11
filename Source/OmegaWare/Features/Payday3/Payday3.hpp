#pragma once

#if OMEGAWARE_ENGINE_UNREAL

#include "Engines/Unreal/Unreal.hpp"
#include "Memory/Safety.hpp"

#pragma warning(push)
#pragma warning(disable : 4005 4369 4309 4267 4244)
#include "SDK/Starbreeze_classes.hpp"
#pragma warning(pop)

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <string_view>
#include <vector>

namespace OmegaWare::Payday3
{
	enum class EEnemyCategory : std::uint8_t
	{
		None,
		Cop,
		Civilian,
	};

	enum class EEnemyType : std::uint8_t
	{
		None,
		Security,
		ArmedCop,
		Shield,
		Dozer,
		Cloaker,
		Sniper,
		Taser,
		Tower,
		Shotgun,
		AR,
		SMG,
		Grenadier,
		Civilian,
		SentryGun,
		Drone,
	};

	enum class EItemCategory : std::uint8_t
	{
		None,
		Loot,
		KeyItem,
		KeycardReader,
		Supply,
	};

	enum class EItemType : std::uint8_t
	{
		None,
		Cash,
		MoneyMedium,
		MoneyLarge,
		Jewelry,
		DepositBox,
		Keycard,
		QRPhone,
		USB,
		KeycardReader,
		ATM,
		Plank,
		HydrogenChloride,
		MuriaticAcid,
		CausticSoda,
		MedkitSmall,
		MedkitLarge,
		Armor,
		Ammo,
		RFIDTag,
		RareMinerals,
		RareStone,
		Safe,
		Meth,
		Paintings,
		Gold,
		CatFigure,
		Rifles,
		Teddy,
		Evidence,
		Misc,
		Cocaine,
		Lab,
		SatellitePart,
		Container,
		CigarBox,
	};

	struct stItemInfo
	{
		std::string_view strLabel;
		EItemType eType = EItemType::None;
		EItemCategory eCategory = EItemCategory::None;
	};

	struct stEnemyInfo
	{
		std::string_view strName;
		EEnemyCategory eCategory = EEnemyCategory::None;
	};

	[[nodiscard]] bool IsReady();
	void SetAimbotTarget(SDK::AActor* pTarget);
	[[nodiscard]] SDK::AActor* GetAimbotTarget();
	[[nodiscard]] bool IsAimbotTargeting();
	[[nodiscard]] SDK::ASBZPlayerCharacter* GetLocalCharacter();
	[[nodiscard]] SDK::USBZActorContainer* GetAllPawns();

	enum class EModUserKind : std::uint8_t
	{
		None,
		User,
		Developer,
	};

	struct stPawnEntry
	{
		SDK::AActor* pActor = nullptr;
		SDK::UClass* pClass = nullptr;
		SDK::USkeletalMeshComponent* pMesh = nullptr;
		const stEnemyInfo* pInfo = nullptr;
		SDK::APlayerState* pPlayerState = nullptr;
		SDK::FVector vecLocation{};
		std::string strName;
		EModUserKind eModKind = EModUserKind::None;
		bool bPlayer = false;
		bool bDrone = false;
		bool bVip = false;
		bool bAlive = false;
	};

	struct stWorldSnapshot
	{
		SDK::UWorld* pWorld = nullptr;
		SDK::APawn* pLocalPawn = nullptr;
		std::vector<stPawnEntry> aPawns;
	};

	[[nodiscard]] std::shared_ptr<const stWorldSnapshot> GetWorldSnapshot();

	[[nodiscard]] EEnemyType ResolveEnemyType(SDK::AActor* pActor);
	[[nodiscard]] bool IsDrone(const SDK::AActor* pActor);
	[[nodiscard]] bool IsVip(SDK::AActor* pActor);
	[[nodiscard]] std::string_view VipLabel(SDK::AActor* pActor);
	[[nodiscard]] bool IsPawnAlive(SDK::AActor* pActor);
	[[nodiscard]] bool IsCharacterAlive(const SDK::ASBZCharacter* pCharacter);
	[[nodiscard]] SDK::USkeletalMeshComponent* GetPawnMesh(SDK::AActor* pActor);
	[[nodiscard]] SDK::USBZOutlineComponent* GetPawnOutline(SDK::AActor* pActor);
	[[nodiscard]] const stItemInfo& ResolveItem(SDK::AActor* pActor);
	[[nodiscard]] const stEnemyInfo& GetEnemyInfo(EEnemyType eType);

	[[nodiscard]] SDK::ASBZBagTriggerVolume* AsDropOff(SDK::AActor* pActor);
	[[nodiscard]] bool IsAlive(const SDK::UObject* pObject);
	[[nodiscard]] bool IsSameObject(const SDK::UObject* pObject, std::int32_t iIndex);
	[[nodiscard]] EModUserKind GetModUserKind(const SDK::APlayerState* pState);
	[[nodiscard]] std::string PlayerName(const SDK::APlayerState* pState);
	void DrawModUsersMenu();
	[[nodiscard]] bool HandleSpawnMessage(SDK::APlayerState* pSender, const std::wstring& wstrText);
	[[nodiscard]] bool SendModChat(const std::wstring& wstrText);
	[[nodiscard]] std::size_t ModUserCount();
	void NoteDisconnectCause(const SDK::APlayerState* pState, const std::string& strCause);
#ifdef _DEBUG
	void DumpCallTrace(std::string_view strReason);
#else
	inline void DumpCallTrace(std::string_view) {}
#endif
	[[nodiscard]] bool IsAliveActor(const SDK::AActor* pActor);

	template <typename Fn>
	void ForEachLevelActor(SDK::UWorld* pWorld, Fn&& fnVisit)
	{
		if (!pWorld)
			return;
		SDK::TArray<SDK::ULevel*>& aLevels = pWorld->Levels;
		for (int iLevel = 0; iLevel < aLevels.Num(); ++iLevel)
		{
			SDK::ULevel* pLevel = aLevels[iLevel];
			if (!Memory::IsValidObjectPtr(pLevel))
				continue;
			for (int iActor = 0; iActor < pLevel->Actors.Num(); ++iActor)
			{
				SDK::AActor* pActor = pLevel->Actors[iActor];
				if (Memory::IsValidObjectPtr(pActor))
					fnVisit(pActor);
			}
		}
	}

	class clInternalInteraction
	{
	public:
		clInternalInteraction();
		~clInternalInteraction();
		clInternalInteraction(const clInternalInteraction&) = delete;
		clInternalInteraction& operator=(const clInternalInteraction&) = delete;
	};

	[[nodiscard]] bool IsInternalInteraction();
	[[nodiscard]] SDK::FName MakeName(const wchar_t* wszName);
	[[nodiscard]] SDK::FName CachedLiteralName(const wchar_t* wszLiteral);
	[[nodiscard]] SDK::FVector GetActorLocation(SDK::AActor* pActor);
	[[nodiscard]] std::string SafeSubstr(const std::string& strValue, std::size_t uOffset, std::size_t uTrimEnd = 0);
}

#endif
