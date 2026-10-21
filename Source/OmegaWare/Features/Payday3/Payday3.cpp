#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "Engines/Engine.hpp"
#include "Memory/Safety.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace OmegaWare::Payday3
{
	namespace
	{
		struct stEnemyLookup
		{
			std::string_view strKeyword;
			EEnemyType eType;
		};

		struct stItemLookup
		{
			std::string_view strKeyword;
			stItemInfo stInfo;
		};

		constexpr std::array<stEnemyLookup, 13> g_aEnemyLookup = { {
			{ "security", EEnemyType::Security },
			{ "armedcop", EEnemyType::ArmedCop },
			{ "shield", EEnemyType::Shield },
			{ "dozer", EEnemyType::Dozer },
			{ "cloaker", EEnemyType::Cloaker },
			{ "sniper", EEnemyType::Sniper },
			{ "taser", EEnemyType::Taser },
			{ "tower", EEnemyType::Tower },
			{ "shotgun", EEnemyType::Shotgun },
			{ "grenadier", EEnemyType::Grenadier },
			{ "civilian", EEnemyType::Civilian },
			{ "smg", EEnemyType::SMG },
			{ "ar_", EEnemyType::AR },
		} };

		constexpr stItemInfo kNoItem{};

		constexpr std::array<stItemLookup, 29> g_aItemLookup = { {
			{ "carriedredkeycard", { "Red Keycard (carried)", EItemType::Keycard, EItemCategory::KeyItem } },
			{ "carriedbluekeycard", { "Blue Keycard (carried)", EItemType::Keycard, EItemCategory::KeyItem } },
			{ "carried", kNoItem },
			{ "cosmetic", kNoItem },
			{ "charm_", kNoItem },
			{ "keycardrequirement", kNoItem },
			{ "keycardrack", kNoItem },
			{ "redkeycardreader", { "Red Reader", EItemType::KeycardReader, EItemCategory::KeycardReader } },
			{ "bluekeycardreader", { "Blue Reader", EItemType::KeycardReader, EItemCategory::KeycardReader } },
			{ "redcardreaderlock", { "Red Reader", EItemType::KeycardReader, EItemCategory::KeycardReader } },
			{ "bluecardreaderlock", { "Blue Reader", EItemType::KeycardReader, EItemCategory::KeycardReader } },
			{ "keycardreaderbase", { "Keycard Reader", EItemType::KeycardReader, EItemCategory::KeycardReader } },
			{ "cardreaderlockbase", { "Keycard Reader", EItemType::KeycardReader, EItemCategory::KeycardReader } },
			{ "redkeycard", { "Red Keycard", EItemType::Keycard, EItemCategory::KeyItem } },
			{ "bluekeycard", { "Blue Keycard", EItemType::Keycard, EItemCategory::KeyItem } },
			{ "keycardbase", { "Keycard", EItemType::Keycard, EItemCategory::KeyItem } },
			{ "qrphone", { "QR Phone", EItemType::QRPhone, EItemCategory::KeyItem } },
			{ "usbdrive", { "USB", EItemType::USB, EItemCategory::KeyItem } },
			{ "depositbox", { "Deposit Box", EItemType::DepositBox, EItemCategory::KeyItem } },
			{ "instantloot_money", { "Cash", EItemType::Cash, EItemCategory::Loot } },
			{ "moneypile_lowvalue", { "Medium Money", EItemType::MoneyMedium, EItemCategory::Loot } },
			{ "moneypile_01_c_lowvalue", { "Medium Money", EItemType::MoneyMedium, EItemCategory::Loot } },
			{ "interactablemoneypile", { "Large Money", EItemType::MoneyLarge, EItemCategory::Loot } },
			{ "interactablejewelry_highvalue", { "Jewelry", EItemType::Jewelry, EItemCategory::Loot } },
			{ "interactablejewelry_lowvalue", { "Jewelry", EItemType::Jewelry, EItemCategory::Loot } },
			{ "instantloot_boxjewelry", { "Jewelry", EItemType::Jewelry, EItemCategory::Loot } },
			{ "instantloot_braceletbox", { "Bracelet", EItemType::Jewelry, EItemCategory::Loot } },
			{ "instantloot_necklacebust", { "Necklace", EItemType::Jewelry, EItemCategory::Loot } },
			{ "interactablejewelry", { "Jewelry", EItemType::Jewelry, EItemCategory::Loot } },
		} };

		constexpr std::array<stEnemyInfo, 15> g_aEnemyInfo = { {
			{ "Unknown", EEnemyCategory::None },
			{ "Security", EEnemyCategory::Cop },
			{ "Armed Cop", EEnemyCategory::Cop },
			{ "Shield", EEnemyCategory::Cop },
			{ "Dozer", EEnemyCategory::Cop },
			{ "Cloaker", EEnemyCategory::Cop },
			{ "Sniper", EEnemyCategory::Cop },
			{ "Taser", EEnemyCategory::Cop },
			{ "Tower", EEnemyCategory::Cop },
			{ "Shotgun", EEnemyCategory::Cop },
			{ "AR", EEnemyCategory::Cop },
			{ "SMG", EEnemyCategory::Cop },
			{ "Grenadier", EEnemyCategory::Cop },
			{ "Civilian", EEnemyCategory::Civilian },
			{ "Sentry Gun", EEnemyCategory::Cop },
		} };

		struct stTypeCache
		{
			std::mutex mtxCache;
			SDK::UWorld* pWorld = nullptr;
			std::unordered_map<SDK::UStruct*, std::string> mapLowerNames;
			std::unordered_map<SDK::UClass*, EEnemyType> mapEnemies;
			std::unordered_map<SDK::UClass*, const stItemInfo*> mapItems;
		};

		stTypeCache g_stCache;

		void ResetOnWorldChange()
		{
			SDK::UWorld* pWorld = Unreal::GetWorld();
			if (pWorld == g_stCache.pWorld)
				return;
			g_stCache.pWorld = pWorld;
			g_stCache.mapLowerNames.clear();
			g_stCache.mapEnemies.clear();
			g_stCache.mapItems.clear();
		}

		const std::string& LowerStructName(SDK::UStruct* pStruct)
		{
			auto itName = g_stCache.mapLowerNames.find(pStruct);
			if (itName != g_stCache.mapLowerNames.end())
				return itName->second;
			std::string strName = pStruct->Name.ToString();
			std::ranges::transform(strName, strName.begin(), [](unsigned char chLetter) { return static_cast<char>(std::tolower(chLetter)); });
			return g_stCache.mapLowerNames.emplace(pStruct, std::move(strName)).first->second;
		}

		template <typename Type, typename Lookup, std::size_t uCount>
		Type ResolveType(std::unordered_map<SDK::UClass*, Type>& rCache, SDK::AActor* pActor, const std::array<Lookup, uCount>& aLookups)
		{
			if (!Memory::IsValidObjectPtr(pActor) || !pActor->Class)
				return Type::None;

			std::scoped_lock lckCache(g_stCache.mtxCache);
			ResetOnWorldChange();
			if (auto itType = rCache.find(pActor->Class); itType != rCache.end())
				return itType->second;

			Type eType = Type::None;
			for (SDK::UStruct* pStruct = pActor->Class; pStruct && eType == Type::None; pStruct = static_cast<SDK::UStruct*>(pStruct->SuperStruct))
			{
				const std::string& strName = LowerStructName(pStruct);
				for (const auto& stEntry : aLookups)
				{
					if (strName.find(stEntry.strKeyword) != std::string::npos)
					{
						eType = stEntry.eType;
						break;
					}
				}
			}

			rCache.emplace(pActor->Class, eType);
			return eType;
		}
	}

	bool IsReady()
	{
		return Engine::IsReady() && Unreal::GetWorld();
	}

	SDK::ASBZPlayerCharacter* GetLocalCharacter()
	{
		if (!IsReady())
			return nullptr;
		SDK::APawn* pPawn = Unreal::GetLocalPawn();
		if (!pPawn || !pPawn->IsA(SDK::ASBZPlayerCharacter::StaticClass()))
			return nullptr;
		return static_cast<SDK::ASBZPlayerCharacter*>(pPawn);
	}

	SDK::USBZActorContainer* GetAllPawns()
	{
		if (!IsReady())
			return nullptr;
		SDK::USBZWorldRuntime* pRuntime = SDK::USBZWorldRuntime::Get(Unreal::GetWorld());
		if (!Memory::IsValidObjectPtr(pRuntime) || !Memory::IsValidObjectPtr(pRuntime->AllPawns))
			return nullptr;
		return pRuntime->AllPawns;
	}

	EEnemyType ResolveEnemyType(SDK::AActor* pActor)
	{
		return ResolveType(g_stCache.mapEnemies, pActor, g_aEnemyLookup);
	}

	const stItemInfo& ResolveItem(SDK::AActor* pActor)
	{
		if (!Memory::IsValidObjectPtr(pActor) || !pActor->Class)
			return kNoItem;

		std::scoped_lock lckCache(g_stCache.mtxCache);
		ResetOnWorldChange();
		if (auto itItem = g_stCache.mapItems.find(pActor->Class); itItem != g_stCache.mapItems.end())
			return *itItem->second;

		const stItemInfo* pInfo = nullptr;
		for (SDK::UStruct* pStruct = pActor->Class; pStruct && !pInfo; pStruct = static_cast<SDK::UStruct*>(pStruct->SuperStruct))
		{
			const std::string& strName = LowerStructName(pStruct);
			const auto itEntry = std::ranges::find_if(g_aItemLookup, [&](const stItemLookup& rEntry) { return strName.find(rEntry.strKeyword) != std::string::npos; });
			if (itEntry != g_aItemLookup.end())
				pInfo = &itEntry->stInfo;
		}
		if (!pInfo)
			pInfo = &kNoItem;
		g_stCache.mapItems.emplace(pActor->Class, pInfo);
		return *pInfo;
	}

	const stEnemyInfo& GetEnemyInfo(EEnemyType eType)
	{
		const auto uIndex = static_cast<std::size_t>(eType);
		return uIndex < g_aEnemyInfo.size() ? g_aEnemyInfo[uIndex] : g_aEnemyInfo[0];
	}

	bool IsAlive(const SDK::UObject* pObject)
	{
		if (!Memory::IsValidObjectPtr(pObject))
			return false;
		constexpr std::uint32_t uDying = static_cast<std::uint32_t>(SDK::EObjectFlags::BeginDestroyed) | static_cast<std::uint32_t>(SDK::EObjectFlags::FinishDestroyed) | static_cast<std::uint32_t>(SDK::EObjectFlags::MirroredGarbage);
		return (static_cast<std::uint32_t>(pObject->Flags) & uDying) == 0;
	}

	bool IsAliveActor(const SDK::AActor* pActor)
	{
		return IsAlive(pActor) && !pActor->bActorIsBeingDestroyed;
	}

	SDK::ASBZBagTriggerVolume* AsDropOff(SDK::AActor* pActor)
	{
		if (!Memory::IsValidObjectPtr(pActor) || !pActor->IsA(SDK::ASBZBagTriggerVolume::StaticClass()))
			return nullptr;
		auto* pVolume = static_cast<SDK::ASBZBagTriggerVolume*>(pActor);
		return pVolume->TriggerMode == SDK::ESBZBagTriggerMode::SecureAndDestroy ? pVolume : nullptr;
	}

	SDK::FName MakeName(const wchar_t* wszName)
	{
		return SDK::UKismetStringLibrary::Conv_StringToName(SDK::FString(wszName));
	}

	std::string SafeSubstr(const std::string& strValue, std::size_t uOffset, std::size_t uTrimEnd)
	{
		if (strValue.size() < uOffset + uTrimEnd)
			return {};
		return strValue.substr(uOffset, strValue.size() - uOffset - uTrimEnd);
	}
}

#endif
