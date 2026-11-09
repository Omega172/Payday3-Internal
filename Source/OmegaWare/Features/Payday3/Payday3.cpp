#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "Engines/Engine.hpp"
#include "Memory/Safety.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace OmegaWare::Payday3
{
	namespace
	{
		constexpr std::size_t kObjectItemFlagsOffset = 0x8;

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

		constexpr std::array<stEnemyLookup, 14> g_aEnemyLookup = { {
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
			{ "cop_", EEnemyType::ArmedCop },
		} };

		constexpr stItemInfo kNoItem{};

		constexpr std::array<stItemLookup, 57> g_aItemLookup = { {
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
			{ "rfidtagred", { "Red RFID Tag", EItemType::RFIDTag, EItemCategory::KeyItem } },
			{ "rfidtagblue", { "Blue RFID Tag", EItemType::RFIDTag, EItemCategory::KeyItem } },
			{ "interactablerareminerals", { "Rare Minerals", EItemType::RareMinerals, EItemCategory::Loot } },
			{ "instantloot_cocaine", { "Cocaine", EItemType::Cocaine, EItemCategory::Loot } },
			{ "interactablecocaine", { "Cocaine", EItemType::Cocaine, EItemCategory::Loot } },
			{ "cocainepile", { "Cocaine", EItemType::Cocaine, EItemCategory::Loot } },
			{ "cookingstation", { "Lab", EItemType::Lab, EItemCategory::KeycardReader } },
			{ "interactablesatellite", { "Satellite Part", EItemType::SatellitePart, EItemCategory::Loot } },
			{ "baggablecigarbox", { "Cigar Box", EItemType::CigarBox, EItemCategory::Loot } },
			{ "rfidtagreaderblue", { "Blue RFID Reader", EItemType::KeycardReader, EItemCategory::KeycardReader } },
			{ "rfidtagreaderred", { "Red RFID Reader", EItemType::KeycardReader, EItemCategory::KeycardReader } },
			{ "qrreader", { "QR Reader", EItemType::KeycardReader, EItemCategory::KeycardReader } },
			{ "depositbox", { "Deposit Box", EItemType::DepositBox, EItemCategory::KeyItem } },
			{ "breakable_atm", { "ATM", EItemType::ATM, EItemCategory::Loot } },
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
			{ "plankspilerequirement", kNoItem },
			{ "plankspile", { "Planks", EItemType::Plank, EItemCategory::Loot } },
			{ "meth_hydrogenchloride", { "Hydrogen Chloride", EItemType::HydrogenChloride, EItemCategory::KeyItem } },
			{ "meth_muriaticacid", { "Muriatic Acid", EItemType::MuriaticAcid, EItemCategory::KeyItem } },
			{ "meth_causticsoda", { "Caustic Soda", EItemType::CausticSoda, EItemCategory::KeyItem } },
			{ "ghost", kNoItem },
			{ "destroyed", kNoItem },
			{ "preview", kNoItem },
			{ "firstaidpack", { "Medkit (small)", EItemType::MedkitSmall, EItemCategory::Supply } },
			{ "doctorbag", { "Medkit (large)", EItemType::MedkitLarge, EItemCategory::Supply } },
			{ "armorrepairkit", { "Armor", EItemType::Armor, EItemCategory::Supply } },
			{ "armorbag", { "Armor", EItemType::Armor, EItemCategory::Supply } },
			{ "ammopickup", { "Ammo", EItemType::Ammo, EItemCategory::Supply } },
			{ "ammopack", { "Ammo", EItemType::Ammo, EItemCategory::Supply } },
			{ "ammobag", { "Ammo", EItemType::Ammo, EItemCategory::Supply } },
		} };

		constexpr std::array<stEnemyInfo, 16> g_aEnemyInfo = { {
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
			{ "Drone", EEnemyCategory::Cop },
		} };

		struct stTypeCache
		{
			std::mutex mtxCache;
			SDK::UWorld* pWorld = nullptr;
			std::unordered_map<SDK::UStruct*, std::string> mapLowerNames;
			std::unordered_map<SDK::UClass*, EEnemyType> mapEnemies;
			std::unordered_map<SDK::UClass*, bool> mapVips;
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
			g_stCache.mapVips.clear();
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

	namespace
	{
		std::atomic<SDK::AActor*> g_pAimbotTarget = nullptr;
	}

	namespace
	{
		std::atomic<std::int64_t> g_iAimbotTargetMs = 0;

		[[nodiscard]] std::int64_t SteadyMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}
	}

	void SetAimbotTarget(SDK::AActor* pTarget)
	{
		g_pAimbotTarget = pTarget;
		if (pTarget)
			g_iAimbotTargetMs.store(SteadyMs(), std::memory_order_relaxed);
	}

	bool IsAimbotTargeting()
	{
		return g_pAimbotTarget.load() != nullptr || SteadyMs() - g_iAimbotTargetMs.load(std::memory_order_relaxed) < 150;
	}

	SDK::AActor* GetAimbotTarget()
	{
		return g_pAimbotTarget;
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

	bool IsDrone(const SDK::AActor* pActor)
	{
		return Memory::IsValidObjectPtr(pActor) && pActor->IsA(SDK::ASBZAIDrone::StaticClass());
	}

	bool IsPawnAlive(SDK::AActor* pActor)
	{
		if (!IsAliveActor(pActor))
			return false;
		if (IsDrone(pActor))
		{
			SDK::USBZAIDroneAttributeSet* pAttributes = static_cast<SDK::ASBZAIDrone*>(pActor)->AttributeSet;
			return !Memory::IsValidObjectPtr(pAttributes) || pAttributes->Health.CurrentValue > 0.f;
		}
		return static_cast<SDK::ASBZCharacter*>(pActor)->bIsAlive;
	}

	SDK::USkeletalMeshComponent* GetPawnMesh(SDK::AActor* pActor)
	{
		if (!Memory::IsValidObjectPtr(pActor))
			return nullptr;
		SDK::USkeletalMeshComponent* pMesh = static_cast<SDK::ACharacter*>(pActor)->Mesh;
		return Memory::IsValidObjectPtr(pMesh) ? pMesh : nullptr;
	}

	SDK::USBZOutlineComponent* GetPawnOutline(SDK::AActor* pActor)
	{
		if (!Memory::IsValidObjectPtr(pActor))
			return nullptr;
		return IsDrone(pActor) ? static_cast<SDK::ASBZAIDrone*>(pActor)->OutlineComponent : static_cast<SDK::ASBZCharacter*>(pActor)->OutlineComponent;
	}

	bool IsVip(SDK::AActor* pActor)
	{
		if (!Memory::IsValidObjectPtr(pActor) || !pActor->Class)
			return false;

		std::scoped_lock lckCache(g_stCache.mtxCache);
		ResetOnWorldChange();
		if (auto itVip = g_stCache.mapVips.find(pActor->Class); itVip != g_stCache.mapVips.end())
			return itVip->second;

		bool bVip = false;
		for (SDK::UStruct* pStruct = pActor->Class; pStruct && !bVip; pStruct = static_cast<SDK::UStruct*>(pStruct->SuperStruct))
		{
			const std::string& strName = LowerStructName(pStruct);
			bVip = strName.find("vip") != std::string::npos || strName.find("civilian_bank_manager") != std::string::npos || strName.find("insider") != std::string::npos;
		}
		g_stCache.mapVips.emplace(pActor->Class, bVip);
		return bVip;
	}

	EEnemyType ResolveEnemyType(SDK::AActor* pActor)
	{
		if (Memory::IsValidObjectPtr(pActor) && pActor->IsA(SDK::ASBZPlayerCharacter::StaticClass()))
			return EEnemyType::None;
		if (IsDrone(pActor))
			return EEnemyType::Drone;
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
		const SDK::TUObjectArray* pObjects = SDK::UObject::GObjects.GetTypedPtr();
		const std::int32_t iIndex = pObject->Index;
		if (!pObjects || iIndex < 0 || iIndex >= pObjects->NumElements)
			return false;
		const SDK::FUObjectItem* pChunk = pObjects->GetDecrytedObjPtr()[iIndex / SDK::TUObjectArray::ElementsPerChunk];
		if (!pChunk)
			return false;
		const SDK::FUObjectItem& rItem = pChunk[iIndex % SDK::TUObjectArray::ElementsPerChunk];
		if (rItem.Object != pObject)
			return false;
		constexpr std::uint32_t uUnreachableOrGarbage = (1u << 28) | (1u << 21);
		if (Unreal::FieldAt<const std::uint32_t>(&rItem, kObjectItemFlagsOffset) & uUnreachableOrGarbage)
			return false;
		constexpr std::uint32_t uDying = static_cast<std::uint32_t>(SDK::EObjectFlags::BeginDestroyed) | static_cast<std::uint32_t>(SDK::EObjectFlags::FinishDestroyed) | static_cast<std::uint32_t>(SDK::EObjectFlags::MirroredGarbage);
		return (static_cast<std::uint32_t>(pObject->Flags) & uDying) == 0;
	}

	bool IsSameObject(const SDK::UObject* pObject, std::int32_t iIndex)
	{
		return IsAlive(pObject) && pObject->Index == iIndex;
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
		return Unreal::MakeName(wszName);
	}

	namespace
	{
		thread_local int t_iInternalInteractions = 0;
	}

	clInternalInteraction::clInternalInteraction()
	{
		++t_iInternalInteractions;
	}

	clInternalInteraction::~clInternalInteraction()
	{
		--t_iInternalInteractions;
	}

	bool IsInternalInteraction()
	{
		return t_iInternalInteractions > 0;
	}

	SDK::FName CachedLiteralName(const wchar_t* wszLiteral)
	{
		return Unreal::CachedLiteralName(wszLiteral);
	}

	SDK::FVector GetActorLocation(SDK::AActor* pActor)
	{
		return Unreal::GetActorLocation(pActor);
	}

	std::string SafeSubstr(const std::string& strValue, std::size_t uOffset, std::size_t uTrimEnd)
	{
		if (strValue.size() < uOffset + uTrimEnd)
			return {};
		return strValue.substr(uOffset, strValue.size() - uOffset - uTrimEnd);
	}
}

#endif
