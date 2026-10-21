#pragma once

#if OMEGAWARE_ENGINE_UNREAL

#include "Engines/Unreal/Unreal.hpp"
#include "Memory/Safety.hpp"

#pragma warning(push)
#pragma warning(disable : 4005 4369 4309 4267 4244)
#include "SDK/Starbreeze_classes.hpp"
#pragma warning(pop)

#include <cstdint>
#include <string>
#include <type_traits>
#include <string_view>

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
	};

	enum class EItemCategory : std::uint8_t
	{
		None,
		Loot,
		KeyItem,
		KeycardReader,
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
	[[nodiscard]] SDK::ASBZPlayerCharacter* GetLocalCharacter();
	[[nodiscard]] SDK::USBZActorContainer* GetAllPawns();

	[[nodiscard]] EEnemyType ResolveEnemyType(SDK::AActor* pActor);
	[[nodiscard]] const stItemInfo& ResolveItem(SDK::AActor* pActor);
	[[nodiscard]] const stEnemyInfo& GetEnemyInfo(EEnemyType eType);

	[[nodiscard]] SDK::ASBZBagTriggerVolume* AsDropOff(SDK::AActor* pActor);
	[[nodiscard]] bool IsAlive(const SDK::UObject* pObject);
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

	[[nodiscard]] SDK::FName MakeName(const wchar_t* wszName);
	[[nodiscard]] std::string SafeSubstr(const std::string& strValue, std::size_t uOffset, std::size_t uTrimEnd = 0);
}

#endif
