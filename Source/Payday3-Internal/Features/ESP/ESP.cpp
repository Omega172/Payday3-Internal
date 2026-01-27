#include <imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>
#include "../../Dumper-7/SDK.hpp"


import Utils.Logging;
import Features.ESP;

// Bone pair structure for skeleton rendering
struct BonePair {
    int32_t ChildIndex;
    int32_t ParentIndex;
};

// Cache bone pairs per skeletal mesh
static std::unordered_map<SDK::USkeletalMesh*, std::vector<BonePair>> g_SkeletonCache;
static std::unordered_map<SDK::USkeletalMesh*, std::vector<BonePair>> g_GuardBonePairsCache;

// Calculate guard bone pairs from skeletal mesh and cache them
const std::vector<BonePair>& CalculateGuardBonePairs(SDK::USkeletalMeshComponent* pMeshComponent) {
    static std::vector<BonePair> empty;
    
    if (!pMeshComponent || !pMeshComponent->SkeletalMesh)
        return empty;

    SDK::USkeletalMesh* pMesh = pMeshComponent->SkeletalMesh;
    
    // Return cached if exists
    auto it = g_GuardBonePairsCache.find(pMesh);
    if (it != g_GuardBonePairsCache.end())
        return it->second;

    // Calculate bone pairs by following parent hierarchy from specific end bones
    std::vector<BonePair> pairs;
    std::unordered_set<int32_t> processedBones;

    SDK::FName head = SDK::UKismetStringLibrary::Conv_StringToName(L"Head");
    SDK::FName rightHand = SDK::UKismetStringLibrary::Conv_StringToName(L"RightHand");
    SDK::FName leftHand = SDK::UKismetStringLibrary::Conv_StringToName(L"LeftHand");
    SDK::FName rightFoot = SDK::UKismetStringLibrary::Conv_StringToName(L"RightFloor");
    SDK::FName leftFoot = SDK::UKismetStringLibrary::Conv_StringToName(L"LeftFloor");

    // Key end bones to trace back from (head, hands, feet)
    std::vector<int32_t> endBones = {pMeshComponent->GetBoneIndex(head), pMeshComponent->GetBoneIndex(rightHand), pMeshComponent->GetBoneIndex(leftHand), pMeshComponent->GetBoneIndex(rightFoot), pMeshComponent->GetBoneIndex(leftFoot)}; // Head, Right Hand, Left Hand, Right Foot, Left Foot
    
    for (int32_t endBone : endBones) {
        int32_t current = endBone;
        while (current >= 0) {
            SDK::FName boneName = pMeshComponent->GetBoneName(current);
            SDK::FName parentBoneName = pMeshComponent->GetParentBone(boneName);
            int32_t parentIndex = pMeshComponent->GetBoneIndex(parentBoneName);
            
            if (parentIndex >= 0 && parentIndex != current) {
                // Avoid duplicate pairs
                if (processedBones.find(current) == processedBones.end()) {
                    pairs.push_back({current, parentIndex});
                    processedBones.insert(current);
                }
                current = parentIndex;
            } else {
                break;
            }
        }
    }
    
    // Cache and return
    g_GuardBonePairsCache[pMesh] = pairs;
    return g_GuardBonePairsCache[pMesh];
}

// Calculate and cache bone pairs for a skeletal mesh
const std::vector<BonePair>& GetOrCalculateBonePairs(SDK::USkeletalMeshComponent* pMeshComponent) {
    static std::vector<BonePair> empty;
    
    if (!pMeshComponent || !pMeshComponent->SkeletalMesh)
        return empty;

    SDK::USkeletalMesh* pMesh = pMeshComponent->SkeletalMesh;
    
    // Return cached if exists
    auto it = g_SkeletonCache.find(pMesh);
    if (it != g_SkeletonCache.end())
        return it->second;

    // Calculate bone pairs from reference skeleton
    std::vector<BonePair> pairs;
    
    int32_t BoneCount = pMeshComponent->GetNumBones();
    for (int32_t i = 0; i < BoneCount; i++) {
        // Get parent index from bone hierarchy
        SDK::FName boneName = pMeshComponent->GetParentBone(pMeshComponent->GetBoneName(i));
        int32_t ParentIndex = pMeshComponent->GetBoneIndex(boneName);
        
        // Skip root bone (parent index -1)
        if (ParentIndex >= 0 && ParentIndex != i) {
            pairs.push_back({i, ParentIndex});
        }
    }
    
    // Cache and return
    g_SkeletonCache[pMesh] = pairs;
    return g_SkeletonCache[pMesh];
}

std::optional<ImVec4> CalculateScreenBoxFromBBox(SDK::APlayerController* pPlayerController,SDK::FVector vecOrigin, SDK::FVector vecExtent)
{
    SDK::FVector aBox[]{
        vecOrigin + SDK::FVector(vecExtent.X, vecExtent.Y, vecExtent.Z),
        vecOrigin + SDK::FVector(-vecExtent.X, vecExtent.Y, vecExtent.Z),
        vecOrigin + SDK::FVector(vecExtent.X, -vecExtent.Y, vecExtent.Z),
        vecOrigin + SDK::FVector(-vecExtent.X, -vecExtent.Y, vecExtent.Z),
        vecOrigin + SDK::FVector(vecExtent.X, vecExtent.Y, -vecExtent.Z),
        vecOrigin + SDK::FVector(-vecExtent.X, vecExtent.Y, -vecExtent.Z),
        vecOrigin + SDK::FVector(vecExtent.X, -vecExtent.Y, -vecExtent.Z),
        vecOrigin + SDK::FVector(-vecExtent.X, -vecExtent.Y, -vecExtent.Z)
    };

    float flMinX = std::numeric_limits<float>::max(), flMaxX = std::numeric_limits<float>::lowest(), flMinY = std::numeric_limits<float>::max(), flMaxY = std::numeric_limits<float>::lowest();

    for(size_t i = 0; i < 8; ++i){
        SDK::FVector2D vec2ScreenPos;
        if(!pPlayerController->ProjectWorldLocationToScreen(aBox[i], &vec2ScreenPos, false))
            continue;

        flMinX = std::min(flMinX, vec2ScreenPos.X);
        flMaxX = std::max(flMaxX, vec2ScreenPos.X);
        flMinY = std::min(flMinY, vec2ScreenPos.Y);
        flMaxY = std::max(flMaxY, vec2ScreenPos.Y);
    }

    if(flMinX == std::numeric_limits<float>::max() || flMaxX == std::numeric_limits<float>::lowest() || flMinY == std::numeric_limits<float>::max() || flMaxY == std::numeric_limits<float>::lowest() ||
        flMinX >= flMaxX || flMinY >= flMaxY)
        return{};

    return ImVec4{ flMinX, flMinY, flMaxX, flMaxY };
}

std::optional<ImVec4> CalculateScreenBoxFromTopBottom(SDK::APlayerController* pPlayerController, SDK::AActor* pActor, SDK::FVector vecTop, SDK::FVector vecBottom, float flCrouchingHeight = 80.f, float flBaseWidthRatio = 0.25f, float flSidewaysWidthRatioAdd = 0.15f, float flCrouchingWidthRatioMultiplier = 1.2f)
{
    SDK::FVector2D vec2Top, vec2Bottom;
    if (!pPlayerController->ProjectWorldLocationToScreen(vecBottom, &vec2Bottom, false) ||
        !pPlayerController->ProjectWorldLocationToScreen(vecTop, &vec2Top, false))
        return {};

    float flHeight = std::abs(vec2Top.Y - vec2Bottom.Y);
    float flYawRad = pActor->K2_GetActorRotation().Yaw * (3.14159265358979323846f / 180.0f);
    float flWidthRatio = flBaseWidthRatio + (std::abs(SDK::FVector(std::cos(flYawRad), std::sin(flYawRad), 0.f).GetNormalized().Dot((pActor->K2_GetActorLocation() - pPlayerController->PlayerCameraManager->GetCameraLocation()).GetNormalized())) * flSidewaysWidthRatioAdd); // Range: 0.25 to 0.4
    
    if (flHeight < flCrouchingHeight)
        flWidthRatio *= flCrouchingWidthRatioMultiplier;

    float flWidth = flHeight * flWidthRatio;
    float flCenter = (vec2Bottom.X + vec2Top.X) / 2.0f;
    return ImVec4{ flCenter - (flWidth / 2.0f), std::min(vec2Bottom.Y, vec2Top.Y), flCenter + (flWidth / 2.0f), std::max(vec2Bottom.Y, vec2Top.Y) };
}

std::optional<ImVec4> CalculateScreenBoxForGuard(SDK::USkeletalMeshComponent* pMeshComponent, SDK::APlayerController* pPlayerController, SDK::AActor* pActor)
{
    if (!pMeshComponent || !pPlayerController || !pActor)
        return {};

    return CalculateScreenBoxFromTopBottom(
        pPlayerController, 
        pActor, 
        pMeshComponent->GetSocketLocation(SDK::UKismetStringLibrary::Conv_StringToName(L"HeadEnd")), 
        pMeshComponent->GetSocketLocation(SDK::UKismetStringLibrary::Conv_StringToName(L"Reference"))
    );
}

// Left, Top, Right, Bottom
std::optional<ImVec4> CalculateScreenBoxUsingBounds(SDK::APlayerController* pPlayerController, SDK::AActor* pActor)
{
    if (!pPlayerController || !pActor)
        return {};

    //auto bbox = pMeshComponent->GetTightBounds(false);
    SDK::FVector vecOrigin{};
    SDK::FVector vecExtent{};

    pActor->GetActorBounds(true, &vecOrigin, &vecExtent, false);
    return CalculateScreenBoxFromBBox(pPlayerController, vecOrigin, vecExtent);
}

namespace ESP
{
    // ESP Configuration
    
    void EnemyESP::UpdatePreviewText(){
        m_sPreviewText = "";

        auto fnAppend = [](std::string& sText, const char* szAppendText, bool bAppend) {
            if (!bAppend)
                return;

            if (sText.size())
                sText += ", ";

            sText += szAppendText;
        };

        fnAppend(m_sPreviewText, "Box", m_bBox);
        fnAppend(m_sPreviewText, "Health", m_bHealth);
        fnAppend(m_sPreviewText, "Armor", m_bArmor);
        fnAppend(m_sPreviewText, "Name", m_bName);
        fnAppend(m_sPreviewText, "Flags", m_bFlags);
        fnAppend(m_sPreviewText, "Skeleton", m_bSkeleton);
        fnAppend(m_sPreviewText, "Outline", m_bOutline);

        if (!m_sPreviewText.size())
            m_sPreviewText = "None";
    };


    enum class EActorType{
        // tazer mine, objective items, ammo boxes, fbi van, drones
        Other,
        Guard,
        Shield,
        Dozer,
        Cloaker,
        Sniper,
        Grenadier,
        Taser,
        Techie,
        Civilian,
        ObjectiveItem,
        InteractableItem,
        LootBag,
        
        Max
    };

    
    EActorType DetermineActorType(SDK::AActor* pActor){
        if (pActor->IsA(SDK::ACH_SWAT_SHIELD_C::StaticClass()))
            return EActorType::Shield;
        else if (pActor->IsA(SDK::ACH_Dozer_C::StaticClass()))
            return EActorType::Dozer;
        else if (pActor->IsA(SDK::ACH_Cloaker_C::StaticClass()))
            return EActorType::Cloaker;
        else if (pActor->IsA(SDK::ACH_Sniper_C::StaticClass()))
            return EActorType::Sniper;
        else if (pActor->IsA(SDK::ACH_Grenadier_C::StaticClass()))
            return EActorType::Grenadier;
        else if (pActor->IsA(SDK::ACH_Taser_C::StaticClass()))
            return EActorType::Taser;
        else if (pActor->IsA(SDK::ACH_Tower_C::StaticClass()))
            return EActorType::Techie;

        if(pActor->IsA(SDK::ACH_BaseCop_C::StaticClass()))
            return EActorType::Guard;

        if(pActor->IsA(SDK::ACH_BaseCivilian_C::StaticClass()))
            return EActorType::Civilian;

        static std::vector<SDK::UClass*> aObjectiveItemClasses = {
            SDK::ABP_RFIDTagBase_C::StaticClass(),
            SDK::ABP_KeycardBase_C::StaticClass(),
            SDK::ABP_CarriedInteractableBase_C::StaticClass(),
            SDK::UGE_CarKeys_C::StaticClass(),
            SDK::UGA_Phone_C::StaticClass()
        };
        if(!std::none_of(aObjectiveItemClasses.begin(), aObjectiveItemClasses.end(), [pActor](SDK::UClass* pClass) { return pActor->IsA(pClass); }))
            return EActorType::ObjectiveItem;
        
        if(pActor->IsA(SDK::ASBZInteractionActor::StaticClass()))
            return EActorType::InteractableItem;

        if(pActor->IsA(SDK::ABP_BaseValuableBag_C::StaticClass()))
            return EActorType::LootBag;

        return EActorType::Other;        
    }

    void DrawSkeleton(SDK::USkeletalMeshComponent* pMeshComponent, SDK::APlayerController* pPlayerController, ImDrawList* pDrawList) {
        if (!pMeshComponent || !pPlayerController || !pDrawList)
            return;

        // Get calculated bone pairs
        const auto& bonePairs = CalculateGuardBonePairs(pMeshComponent);

        // Draw bone indices/names
        if (GetConfig().bDebugDrawBoneIndices) {
            // Create set of bone indices that appear in bone pairs
            std::unordered_set<int32_t> usedBones;
            for (const auto& pair : bonePairs) {
                // Exclude bone 0
                if (pair.ChildIndex == 0 || pair.ParentIndex == 0)
                    continue;
                    
                usedBones.insert(pair.ChildIndex);
                usedBones.insert(pair.ParentIndex);
            }
            
            for (int32_t i : usedBones) {
                SDK::FVector BonePos = pMeshComponent->GetSocketLocation(pMeshComponent->GetBoneName(i));
                SDK::FVector2D ScreenPos;
                if (pPlayerController->ProjectWorldLocationToScreen(BonePos, &ScreenPos, false)) {
                    if (GetConfig().bDebugDrawBoneNames) {
                        SDK::FName boneName = pMeshComponent->GetBoneName(i);
                        std::string nameStr = boneName.ToString();
                        pDrawList->AddText(
                            ImVec2(ScreenPos.X, ScreenPos.Y),
                            IM_COL32(0, 255, 0, 255),
                            nameStr.c_str()
                        );
                        continue;
                    }

                    char szIndex[16];
                    sprintf_s(szIndex, "%d", i);
                    pDrawList->AddText(
                        ImVec2(ScreenPos.X, ScreenPos.Y),
                        IM_COL32(0, 255, 0, 255),
                        szIndex
                    );
                }
            }
        }

        for (const auto& pair : bonePairs) {
            // Exclude bone 0
            if (pair.ChildIndex == 0 || pair.ParentIndex == 0)
                continue;

            // Get bone transforms in world space
            SDK::FVector ChildPos = pMeshComponent->GetSocketLocation(pMeshComponent->GetBoneName(pair.ChildIndex));
            SDK::FVector ParentPos = pMeshComponent->GetSocketLocation(pMeshComponent->GetBoneName(pair.ParentIndex));
            
            // Project to screen
            SDK::FVector2D ChildScreen, ParentScreen;
            if (pPlayerController->ProjectWorldLocationToScreen(ChildPos, &ChildScreen, false) &&
                pPlayerController->ProjectWorldLocationToScreen(ParentPos, &ParentScreen, false)) {
                
                // Draw line between bones
                pDrawList->AddLine(
                    ImVec2(ParentScreen.X, ParentScreen.Y),
                    ImVec2(ChildScreen.X, ChildScreen.Y),
                    IM_COL32(255, 255, 0, 255),
                    2.0f
                );
            }
        }
    }

    void DrawDebugSkeleton(SDK::USkeletalMeshComponent* pMeshComponent, SDK::APlayerController* pPlayerController, ImDrawList* pDrawList) {
        if (!pMeshComponent || !pPlayerController || !pDrawList)
            return;
        
        // Draw bone indices/names
        if (GetConfig().bDebugSkeletonDrawBoneIndices) {
            int32_t BoneCount = pMeshComponent->GetNumBones();
            for (int32_t i = 0; i < BoneCount; i++) {
                SDK::FVector BonePos = pMeshComponent->GetSocketLocation(pMeshComponent->GetBoneName(i));
                SDK::FVector2D ScreenPos;
                if (pPlayerController->ProjectWorldLocationToScreen(BonePos, &ScreenPos, false)) {
                    if (GetConfig().bDebugSkeletonDrawBoneNames) {
                        SDK::FName boneName = pMeshComponent->GetBoneName(i);
                        std::string nameStr = boneName.ToString();
                        pDrawList->AddText(
                            ImVec2(ScreenPos.X, ScreenPos.Y),
                            IM_COL32(0, 255, 0, 255),
                            nameStr.c_str()
                        );
                        continue;
                    }

                    char szIndex[16];
                    sprintf_s(szIndex, "%d", i);
                    pDrawList->AddText(
                        ImVec2(ScreenPos.X, ScreenPos.Y),
                        IM_COL32(0, 255, 0, 255),
                        szIndex
                    );
                }
            }
        }
        
        // Draw debug skeleton using dynamically calculated bone pairs
        if (GetConfig().bDebugSkeleton) {
            const auto& pairs = GetOrCalculateBonePairs(pMeshComponent);
            
            for (const auto& pair : pairs) {
                // Get bone transforms in world space
                SDK::FVector ChildPos = pMeshComponent->GetSocketLocation(pMeshComponent->GetBoneName(pair.ChildIndex));
                SDK::FVector ParentPos = pMeshComponent->GetSocketLocation(pMeshComponent->GetBoneName(pair.ParentIndex));
                
                // Project to screen
                SDK::FVector2D ChildScreen, ParentScreen;
                if (pPlayerController->ProjectWorldLocationToScreen(ChildPos, &ChildScreen, false) &&
                    pPlayerController->ProjectWorldLocationToScreen(ParentPos, &ParentScreen, false)) {
                    
                    // Draw line between bones
                    pDrawList->AddLine(
                        ImVec2(ParentScreen.X, ParentScreen.Y),
                        ImVec2(ChildScreen.X, ChildScreen.Y),
                        IM_COL32(255, 0, 255, 255),
                        2.0f
                    );
                }
            }
        }
    }

    void DrawBar(ImDrawList* pDrawList, ImVec4 vec4ScreenBox, float flPercentage, ImU32 color, float flOffset = 4.f, float flWidth = 5.f)
    {
        if (flPercentage > 1.f)
            flPercentage = 1.f;
        if (flPercentage <= 0.f)
            flPercentage = 0.f;

        float flHeight = vec4ScreenBox.w - vec4ScreenBox.y;
        float flBarX = vec4ScreenBox.x - (flWidth + flOffset);

        // Background
        pDrawList->AddRectFilled(
            ImVec2(flBarX - 1, vec4ScreenBox.y - 1),
            ImVec2(flBarX + flWidth + 1, vec4ScreenBox.y + flHeight + 1),
            IM_COL32(30, 30, 30, 55)
        );

        // Fill
        pDrawList->AddRectFilled(
            ImVec2(flBarX, vec4ScreenBox.y + flHeight * (1.0f - flPercentage)),
            ImVec2(flBarX + flWidth, vec4ScreenBox.y + flHeight),
            color
        );
    }

    void DrawName(ImDrawList* pDrawList, ImVec4 vec4ScreenBox, SDK::AActor* pActor, EActorType eType) {
        std::string sCharacterName = "";
        switch(eType){
        case EActorType::Guard:
            sCharacterName = "Pig";
            break;
        case EActorType::Shield:
            sCharacterName = "Shield";
            break;
        case EActorType::Dozer:
            sCharacterName = "Dozer";
            break;
        case EActorType::Cloaker:
            sCharacterName = "Cloaker";
            break;
        case EActorType::Sniper:
            sCharacterName = "Sniper";
            break;
        case EActorType::Grenadier:
            sCharacterName = "Grenadier";
            break;
        case EActorType::Taser:
            sCharacterName = "Taser";
            break;
        case EActorType::Techie:
            sCharacterName = "Techie";
            break;
        default:
            sCharacterName = pActor->GetName();
            break;
        }

        if (!sCharacterName.size())
            return;

        ImVec2 vec2TextSize = ImGui::CalcTextSize(sCharacterName.c_str());
        ImVec2 vec2TextPos = ImVec2{(vec4ScreenBox.z - vec4ScreenBox.x - vec2TextSize.x) / 2.f + vec4ScreenBox.x, vec4ScreenBox.y - vec2TextSize.y - 4.f};
        
        pDrawList->AddText(ImVec2{ vec2TextPos.x + 1.f, vec2TextPos.y + 1.f }, IM_COL32(30, 30, 30, 55), sCharacterName.c_str());
        pDrawList->AddText(vec2TextPos, IM_COL32(255, 255, 255, 200), sCharacterName.c_str());
    };

    void DrawEnemyESP(ImDrawList* pDrawList, SDK::APlayerController* pPlayerController, SDK::ACH_BaseCop_C* pGuard, EActorType eType, EnemyESP& stSettings){
        pGuard->Multicast_SetMarked(stSettings.m_bOutline);

        SDK::USkeletalMeshComponent* pSkeletalMesh = pGuard->Mesh;
        if (!pSkeletalMesh)
            return;

        // Draw ESP features
        if (stSettings.m_bSkeleton) {
            DrawSkeleton(pSkeletalMesh, pPlayerController, pDrawList);
            DrawDebugSkeleton(pSkeletalMesh, pPlayerController, pDrawList);
        }
        
        if (auto optScreenBox = CalculateScreenBoxForGuard(pSkeletalMesh, pPlayerController, pGuard); optScreenBox.has_value())
        {
            auto vec4ScreenBox = optScreenBox.value();
            if (stSettings.m_bBox){
                pDrawList->AddRect(ImVec2(vec4ScreenBox.x - 1, vec4ScreenBox.y - 1), ImVec2(vec4ScreenBox.z + 1, vec4ScreenBox.w + 1), IM_COL32(30, 30, 30, 55), 0.0f, 0, 2.0f);
                pDrawList->AddRect(ImVec2(vec4ScreenBox.x, vec4ScreenBox.y), ImVec2(vec4ScreenBox.z, vec4ScreenBox.w), IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
            }
                
            if (stSettings.m_bName)
                DrawName(pDrawList, vec4ScreenBox, pGuard, eType);

            float flBarOffset = 4.f;
            if (stSettings.m_bHealth){
                DrawBar(pDrawList, vec4ScreenBox, pGuard->AttributeSet->Health.CurrentValue / pGuard->AttributeSet->HealthMax.CurrentValue, IM_COL32(142, 230, 11, 200), flBarOffset);
                flBarOffset += 8.f;
            }

            if (stSettings.m_bArmor && pGuard->AttributeSet->Armor.CurrentValue > std::numeric_limits<float>::epsilon()){
                DrawBar(pDrawList, vec4ScreenBox, pGuard->AttributeSet->Armor.CurrentValue / pGuard->AttributeSet->ArmorMax.CurrentValue, IM_COL32(64, 147, 255, 200), flBarOffset);
                flBarOffset += 8.f;
            }            


            if (!stSettings.m_bFlags)
                return;
            
            float flFlagsOffset = 0.f;

            auto& aGuardChildren = pGuard->Children;
            for(int i = 0; i < aGuardChildren.Num(); i++){
                SDK::AActor* pChildObj = aGuardChildren[i];
                if(!pChildObj)
                    continue;

                if (DetermineActorType(pChildObj) != EActorType::ObjectiveItem)
                    continue;
                
                pDrawList->AddText(
                    ImVec2(vec4ScreenBox.z + 5.f, vec4ScreenBox.y + flFlagsOffset),
                    IM_COL32(0, 255, 0, 255),
                    (pChildObj->IsA(SDK::ABP_CarriedBlueKeycard_C::StaticClass()) ? "Blue Keycard" : 
                        pChildObj->IsA(SDK::ABP_CarriedRedKeycard_C::StaticClass()) ? "Red Keycard" :
                        pChildObj->IsA(SDK::ABP_CarriedHackablePhone_C::StaticClass()) ? "Phone" :
                        pChildObj->IsA(SDK::ABP_CarriedFakeID_C::StaticClass()) ? "Fake ID" :
                        pChildObj->Name.ToString()).c_str()
                );
                
                flFlagsOffset += 15.f;
            }
        }  
    };

    void DrawInteractableESP(ImDrawList* pDrawList, SDK::APlayerController* pPlayerController, SDK::ASBZInteractionActor* pItem, EActorType eType){
        if (!pItem)
            return;

        if (auto optScreenBox = CalculateScreenBoxUsingBounds(pPlayerController, pItem); optScreenBox.has_value())
        {
            auto vec4ScreenBox = optScreenBox.value();
            DrawName(pDrawList, vec4ScreenBox, pItem, eType);
        }
    }


    bool ShouldSkipActor(SDK::AActor* pActor, EActorType eType){
        switch(eType){
        case EActorType::Guard:
        case EActorType::Shield:
        case EActorType::Dozer:
        case EActorType::Cloaker:
        case EActorType::Sniper:
        case EActorType::Grenadier:
        case EActorType::Taser:
        case EActorType::Techie:
        case EActorType::Civilian:
            return !reinterpret_cast<SDK::ASBZCharacter*>(pActor)->bIsAlive;

        default:
            break;
        }
        return false;
    }

    struct ActorInfo{
        EActorType m_eType;
        float m_flDistance;
        SDK::AActor* m_pActor;
    };

/*
Default__GA_Hacker_RoutedPing_C
Default__GA_Hacker_SecuredLoop_C
Default__GA_PlayerCrouch_C
Default__GA_EquipNextWeapon_C
Default__GA_EquipPreviousWeapon_C
Default__GA_EquipPrimaryWeapon_C
Default__GA_EquipSecondaryWeapon_C
Default__GA_Traverse_C
Default__GA_PlayerJump_C
Default__GA_Run_C
Default__GA_Downed_C
Default__GA_BleedOut_C
Default__GA_Interact_C
Default__GA_Equip_C
Default__GA_Shout_C
Default__GA_ThrowBag_C
Default__GA_Slide_C
Default__GA_Carry_C
Default__GA_ThrowCarry_C
Default__GA_ThrowItem_C
Default__GA_MiniGame_C
Default__GA_Land_C
Default__GA_MaskOnInput_C
Default__SBZPlayerViewTargetAbility
Default__GA_HumanShieldInstigator_C
Default__GA_HumanShieldShove_C
Default__SBZEquipPlaceableAbility
Default__GA_Tased_C
Default__SBZCuffedAbility
Default__SBZEquipNextGadgetAbility
Default__SBZEquipAutoAbility
Default__GA_MaskOnAction_C
Default__GA_Tased_Gently_C
Default__GA_Tased_Uncontrolled_C
Default__SBZKillHumanShieldAbility
Default__SBZSubduedAbility
Default__GA_Tackle_C
Default__SBZToolUnequippedAbility
Default__SBZZiplineAbility
Default__GA_CuttingTool_C
Default__SBZExitPhoneAbility
Default__SBZPlayerRunExitAbility
Default__SBZCanUIActivatePhoneAbility
Default__GA_AnimatedInteraction_C
Default__GA_EquippableInspect_C
Default__GA_HackDrone_C
Default__GA_PlayerWeaponTanking_C
Default__GA_PlayerWeaponWallReaction_C
Default__GA_PlayerThrowBagAnimation_C
Default__GA_PlayerEmote_C
Default__SBZEquipConsumableAbility
Default__GA_UseConsumable_C
Default__SBZSkillHET5AOEDamageAbility
Default__SBZTickOverskillOperatorAbility
Default__GA_Phone_C
Default__GA_PlaceECMJammer_C
Default__GA_RequestOverkillWeapon_C
Default__GA_Fire_C
Default__GA_PlayerTarget_C
Default__GA_Reload_C
Default__GA_Melee_C
Default__GA_PlayerEndCycleReload_C
*/

    SDK::FGameplayAbilitySpec* GetAbilitySpec(SDK::USBZPlayerAbilitySystemComponent* pAbilitySystem, const SDK::FName& nameAbility){
        if (!pAbilitySystem)
            return nullptr;

        auto& aAbilities = pAbilitySystem->ActivatableAbilities.Items;
        for (int i = 0; i < aAbilities.Num(); ++i) {
            if (!aAbilities.IsValidIndex(i))
                continue;

            auto pAbility = aAbilities[i].Ability;
            if (!pAbility)
                continue;

            if (pAbility->Name == nameAbility)
                return &aAbilities[i];
        }

        return nullptr;
    }

    void Render(SDK::UWorld* pGWorld, SDK::APlayerController* pPlayerController) {
        if (!GetConfig().bESP)
            return;

        SDK::USBZWorldRuntime* pWorldRuntime = reinterpret_cast<SDK::USBZWorldRuntime*>(SDK::USBZWorldRuntime::GetWorldRuntime(pGWorld));
        if (!pWorldRuntime)
            return;
        
        ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();
        
        UC::TArray<SDK::UObject*>& actors = pWorldRuntime->AllPawns->Objects;
        UC::TArray<SDK::UObject*>& aliveActors = pWorldRuntime->AllAlivePawns->Objects;

        SDK::FVector vecCameraLocation = pPlayerController->PlayerCameraManager->GetCameraLocation();
        std::vector<ActorInfo> vecActors{};
        vecActors.reserve(actors.Num());
        for (int i = 0; i < actors.Num(); ++i){
            if (!actors.IsValidIndex(i))
                break;

            auto pActor = reinterpret_cast<SDK::AActor*>(actors[i]);
            if(!pActor)
                continue;

            auto eType = DetermineActorType(pActor);
            if (ShouldSkipActor(pActor, eType))
                continue;

            vecActors.emplace_back(ActorInfo{
                .m_eType = eType,
                .m_flDistance = (pActor->K2_GetActorLocation() - vecCameraLocation).Magnitude(),
                .m_pActor = pActor
            });
        }

        

        std::sort(vecActors.begin(), vecActors.end(), [](ActorInfo& lhs, ActorInfo& rhs) {
            if (lhs.m_flDistance == rhs.m_flDistance)
                rhs.m_flDistance += 0.001f;

            return lhs.m_flDistance < rhs.m_flDistance;
        });
        
        SDK::FRotator vecPlayerRotation = pPlayerController->PlayerCameraManager->GetCameraRotation();
        SDK::FVector vecForward = SDK::UKismetMathLibrary::GetForwardVector(vecPlayerRotation);
        SDK::FVector vecLookAheadLocation = vecCameraLocation + (vecForward * 200.f);

        for (ActorInfo& infoActor : vecActors){
            auto pActor = infoActor.m_pActor;
            SDK::FVector2D vec2ScreenLocation;
            if (!pActor)
                continue;

            SDK::FVector vecOffset{};

            if(0){
                switch(infoActor.m_eType){
                case EActorType::Guard:
                case EActorType::Shield:
                case EActorType::Cloaker:
                case EActorType::Sniper:
                case EActorType::Grenadier:
                case EActorType::Taser:
                case EActorType::Techie:;
                    vecOffset = pActor->K2_GetActorLocation() - reinterpret_cast<SDK::ACH_BaseCop_C*>(pActor)->Mesh->GetSocketLocation(SDK::UKismetStringLibrary::Conv_StringToName(L"Head"));
                    pActor->K2_SetActorRotation(vecPlayerRotation, true);
                    pActor->K2_SetActorLocation(vecLookAheadLocation + vecOffset, false, nullptr, true);
                    break;

                case EActorType::Dozer:
                    vecOffset = pActor->K2_GetActorLocation() - reinterpret_cast<SDK::ACH_BaseCop_C*>(pActor)->Mesh->GetSocketLocation(SDK::UKismetStringLibrary::Conv_StringToName(L"Head"));
                    pActor->K2_SetActorRotation(SDK::FRotator(-vecPlayerRotation.Pitch, -vecPlayerRotation.Yaw), true);
                    pActor->K2_SetActorLocation(vecLookAheadLocation + vecOffset, false, nullptr, true);
                    break;

                default:
                    break;
                }
            }
            

            if(!pPlayerController->ProjectWorldLocationToScreen(pActor->K2_GetActorLocation(), &vec2ScreenLocation, false))
                continue;

            switch(infoActor.m_eType){
            case EActorType::Guard:
                DrawEnemyESP(pDrawList, pPlayerController, reinterpret_cast<SDK::ACH_BaseCop_C*>(pActor), infoActor.m_eType, GetConfig().m_stNormalEnemies);
                break;

            case EActorType::Shield:
            case EActorType::Dozer:
            case EActorType::Cloaker:
            case EActorType::Sniper:
            case EActorType::Grenadier:
            case EActorType::Taser:
            case EActorType::Techie:
                DrawEnemyESP(pDrawList, pPlayerController, reinterpret_cast<SDK::ACH_BaseCop_C*>(pActor), infoActor.m_eType, GetConfig().m_stSpecialEnemies);
                break;

            case EActorType::ObjectiveItem:
            case EActorType::InteractableItem:
                DrawInteractableESP(pDrawList, pPlayerController, reinterpret_cast<SDK::ASBZInteractionActor*>(pActor), infoActor.m_eType);
                break;

            default:
                break;
            }           
        }

        auto pLocalPlayer = reinterpret_cast<SDK::ASBZPlayerCharacter*>(pPlayerController->AcknowledgedPawn);
        if (!pLocalPlayer)
            return;

        // No Recoil
        if (pLocalPlayer->RecoilComponent && pLocalPlayer->RecoilComponent->CurrentRecoilData){
            pLocalPlayer->RecoilComponent->CurrentRecoilData->ViewKick.SpeedDeflect = 0.f;
            pLocalPlayer->RecoilComponent->CurrentRecoilData->GunKickXY.SpeedDeflect = 0.f;
        }

        // No Spread
        if (pLocalPlayer->FPCameraAttachment && pLocalPlayer->FPCameraAttachment->EquippedWeaponData){
            if (pLocalPlayer->FPCameraAttachment->EquippedWeaponData->IsA(SDK::USBZRangedWeaponData::StaticClass())){
                auto pWeaponData = reinterpret_cast<SDK::USBZRangedWeaponData*>(pLocalPlayer->FPCameraAttachment->EquippedWeaponData);
            
                if (pWeaponData->SpreadData){
                    auto pSpreadData = pWeaponData->SpreadData;
                    pSpreadData->InnerClusterSpreadMultiplier = pSpreadData->FireSpreadStart = pSpreadData->FireSpreadMinCap = pSpreadData->FireSpreadCap = pSpreadData->FireSpreadIncrease = 0.f;
                }
            }
        }
        
        // No camera shake
        pPlayerController->PlayerCameraManager->StopAllCameraShakes(true);

        auto pAbilitySystem = pLocalPlayer->PlayerAbilitySystem;
        if (!pAbilitySystem)
            return;

        auto& aAbilities = pAbilitySystem->ActivatableAbilities.Items;

        auto pReloadAbility = GetAbilitySpec(pAbilitySystem, SDK::UKismetStringLibrary::Conv_StringToName(L"Default__GA_Reload_C"));
        auto pFireAbility = GetAbilitySpec(pAbilitySystem, SDK::UKismetStringLibrary::Conv_StringToName(L"Default__GA_Fire_C"));

        auto& aCameras = pWorldRuntime->AllSecurityCameras->Objects;
        for (int i = 0; i < aCameras.Num(); ++i) {
            if (!aCameras.IsValidIndex(i))
                break;

            auto pCamera = reinterpret_cast<SDK::ASBZSecurityCamera*>(aCameras[i]);
            if (!pCamera)
                continue;

            if (pCamera->SoundState == SDK::ESBZCameraSoundState::Suspiscious){
                // Use runtime on this camera
            }

            if (pCamera->OutlineAsset)
                pCamera->OutlineAsset->ColorIndex = 3;
            pCamera->OutlineComponent->Multicast_SetActiveReplicated(pCamera->OutlineAsset);
            //pCamera->OutlineComponent->Multicast();
        }

        SDK::ULevel* pPersistentLevel = pGWorld->PersistentLevel;
        pPersistentLevel->Actors;

        GetConfig().m_stNormalEnemies.m_bWasOutlineActive = GetConfig().m_stNormalEnemies.m_bOutline;
        GetConfig().m_stSpecialEnemies.m_bWasOutlineActive = GetConfig().m_stSpecialEnemies.m_bOutline;
    }

    void RenderDebugESP(SDK::ULevel* pPersistentLevel, SDK::APlayerController* pPlayerController) {
        if (!GetConfig().bDebugESP)
            return;

        ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

        for (SDK::AActor* pActor : pPersistentLevel->Actors) {
            if (!pActor)
                continue;

            SDK::FVector ActorLocation = pActor->K2_GetActorLocation();
            SDK::FVector2D ScreenLocation;

            if (!pPlayerController->ProjectWorldLocationToScreen(ActorLocation, &ScreenLocation, false))
                continue;

            char szName[64];
            for (SDK::UStruct* pStruct = static_cast<SDK::UStruct*>(pActor->Class); pStruct != nullptr; pStruct = static_cast<SDK::UStruct*>(pStruct->SuperStruct)) {

                szName[pStruct->Name.GetRawString().copy(szName, 63)] = '\0';

                ImVec2 vecTextSize = ImGui::CalcTextSize(szName);
                pDrawList->AddText({ScreenLocation.X - vecTextSize.x / 2, ScreenLocation.Y - 8.f}, IM_COL32(255, 0, 0, 255), szName);
                ScreenLocation.Y += vecTextSize.y + 2.f;
            }
        }
    }
}
