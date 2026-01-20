module;
#include <imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cmath>
#include "../../Dumper-7/SDK.hpp"

export module Features.ESP;
import Utils.Logging;

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

export namespace ESP
{
    // ESP Configuration
    struct Config {
        bool bESP = false;
        bool bBoxESP = false;
        bool bSkeleton = false;
        bool bDebugSkeleton = false;
        bool bDebugDrawBoneIndices = false;
        bool bDebugDrawBoneNames = false;
        bool bDebugSkeletonDrawBoneIndices = false;
        bool bDebugSkeletonDrawBoneNames = false;
        bool bDebugESP = false;
    };

    inline Config g_Config;

    Config& GetConfig() {
        return g_Config;
    }

    void DrawSkeleton(SDK::USkeletalMeshComponent* pMeshComponent, SDK::APlayerController* pPlayerController, ImDrawList* pDrawList) {
        if (!pMeshComponent || !pPlayerController || !pDrawList)
            return;

        // Get calculated bone pairs
        const auto& bonePairs = CalculateGuardBonePairs(pMeshComponent);

        // Draw bone indices/names
        if (g_Config.bDebugDrawBoneIndices) {
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
                    if (g_Config.bDebugDrawBoneNames) {
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

        // Draw skeleton using calculated bone pairs
        if (g_Config.bSkeleton) {
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
    }

    void DrawDebugSkeleton(SDK::USkeletalMeshComponent* pMeshComponent, SDK::APlayerController* pPlayerController, ImDrawList* pDrawList) {
        if (!pMeshComponent || !pPlayerController || !pDrawList)
            return;
        
        // Draw bone indices/names
        if (g_Config.bDebugSkeletonDrawBoneIndices) {
            int32_t BoneCount = pMeshComponent->GetNumBones();
            for (int32_t i = 0; i < BoneCount; i++) {
                SDK::FVector BonePos = pMeshComponent->GetSocketLocation(pMeshComponent->GetBoneName(i));
                SDK::FVector2D ScreenPos;
                if (pPlayerController->ProjectWorldLocationToScreen(BonePos, &ScreenPos, false)) {
                    if (g_Config.bDebugSkeletonDrawBoneNames) {
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
        if (g_Config.bDebugSkeleton) {
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

    void DrawBox(SDK::USkeletalMeshComponent* pMeshComponent, SDK::APlayerController* pPlayerController, ImDrawList* pDrawList, SDK::AActor* pActor) {
        if (!pMeshComponent || !pPlayerController || !pDrawList || !pActor)
            return;

        if (!g_Config.bBoxESP)
            return;

        // Get bone 0 (Reference - bottom/root) and bone 10 (HeadEnd - top of head)
        SDK::FVector RootPos = pMeshComponent->GetSocketLocation(SDK::UKismetStringLibrary::Conv_StringToName(L"Reference"));
        SDK::FVector HeadPos = pMeshComponent->GetSocketLocation(SDK::UKismetStringLibrary::Conv_StringToName(L"HeadEnd"));

        // Project both bones to screen
        SDK::FVector2D RootScreen, HeadScreen;
        if (!pPlayerController->ProjectWorldLocationToScreen(RootPos, &RootScreen, false) ||
            !pPlayerController->ProjectWorldLocationToScreen(HeadPos, &HeadScreen, false))
            return;

        // Calculate height (distance between root and head on screen)
        float height = abs(HeadScreen.Y - RootScreen.Y);

        // Get camera location and target actor location
        SDK::FVector CameraLocation = pPlayerController->PlayerCameraManager->GetCameraLocation();
        SDK::FVector ActorLocation = pActor->K2_GetActorLocation();
        SDK::FRotator ActorRotation = pActor->K2_GetActorRotation();

        // Calculate view direction (camera to target)
        SDK::FVector ViewDir = ActorLocation - CameraLocation;
        ViewDir.Normalize();

        // Calculate actor's forward vector from rotation (yaw angle)
        // Convert yaw from degrees to radians
        const float PI = 3.14159265358979323846f;
        float YawRadians = ActorRotation.Yaw * (PI / 180.0f);
        
        SDK::FVector ActorForward;
        ActorForward.X = cosf(YawRadians);
        ActorForward.Y = sinf(YawRadians);
        ActorForward.Z = 0.0f;
        ActorForward.Normalize();

        // Calculate dot product to determine viewing angle
        // Dot product: 1 = viewing from front/back, 0 = viewing from side
        float ViewDot = abs(ActorForward.Dot(ViewDir));

        // Interpolate width multiplier based on viewing angle
        // Side view (dot ~0): narrower box (1/4 height)
        // Front/back view (dot ~1): wider box (1/2.5 height)
        float widthMultiplier = 0.25f + (ViewDot * 0.15f); // Range: 0.25 to 0.4
        
        // Check if crouching by comparing actual height to expected standing height
        // Normal standing height ratio should be around certain value, crouching reduces it
        float heightRatio = height / 100.0f; // Adjust base value as needed
        bool bIsCrouching = heightRatio < 0.8f; // Threshold for crouch detection
        
        if (bIsCrouching) {
            // When crouching, slightly increase width multiplier since body is more compact
            widthMultiplier *= 1.2f;
        }

        float width = height * widthMultiplier;

        // Calculate box corners (centered on the character)
        float topY = std::min(RootScreen.Y, HeadScreen.Y);
        float bottomY = std::max(RootScreen.Y, HeadScreen.Y);
        float centerX = (RootScreen.X + HeadScreen.X) / 2.0f;
        float leftX = centerX - (width / 2.0f);
        float rightX = centerX + (width / 2.0f);

        // Draw the bounding box
        pDrawList->AddRect(
            ImVec2(leftX, topY),
            ImVec2(rightX, bottomY),
            IM_COL32(255, 0, 0, 255),
            0.0f,
            0,
            2.0f
        );

        SDK::ACH_BaseCop_C* pGuard = reinterpret_cast<SDK::ACH_BaseCop_C*>(pActor);
        float fCurrentHealth = pGuard->AttributeSet->Health.CurrentValue;
        float fMaxHealth = pGuard->AttributeSet->HealthMax.CurrentValue;

        // Draw health bar
        float healthBarHeight = height;
        float healthBarWidth = 5.0f;
        float healthPercent = fCurrentHealth / fMaxHealth;
        float healthBarX = leftX - 10.0f;
        float healthBarY = topY;
        // Background
        pDrawList->AddRectFilled(
            ImVec2(healthBarX, healthBarY),
            ImVec2(healthBarX + healthBarWidth, healthBarY + healthBarHeight),
            IM_COL32(50, 50, 50, 255)
        );
        // Health fill
        pDrawList->AddRectFilled(
            ImVec2(healthBarX, healthBarY + healthBarHeight * (1.0f - healthPercent)),
            ImVec2(healthBarX + healthBarWidth, healthBarY + healthBarHeight),
            IM_COL32(0, 255, 0, 255)
        );
    }

    void Render(SDK::UWorld* pGWorld, SDK::APlayerController* pPlayerController) {
        if (!g_Config.bESP)
            return;

        SDK::USBZWorldRuntime* pWorldRuntime = reinterpret_cast<SDK::USBZWorldRuntime*>(SDK::USBZWorldRuntime::GetWorldRuntime(pGWorld));
        if (pWorldRuntime) {
            ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();
            
            UC::TArray<SDK::UObject*> actors = pWorldRuntime->AllPawns->Objects;
            UC::TArray<SDK::UObject*> aliveActors = pWorldRuntime->AllAlivePawns->Objects;

            for (int i = 0; i < aliveActors.Num(); i++) {
                SDK::AActor* pActor = reinterpret_cast<SDK::AActor*>(aliveActors[i]);
                if (!pActor || !pActor->IsA(SDK::ACH_BaseCop_C::StaticClass()))
                    continue;

                auto pGuard = reinterpret_cast<SDK::ACH_BaseCop_C*>(aliveActors[i]);
                SDK::FVector2D vec2ScreenLocation;
                if (!pGuard || !pPlayerController->ProjectWorldLocationToScreen(pGuard->K2_GetActorLocation(), &vec2ScreenLocation, false))
                    continue;

            
                if (SDK::USBZOnlineFunctionLibrary::IsSoloGame(pGWorld))
                    pGuard->Multicast_SetMarked(true);

                SDK::USkeletalMeshComponent* pSkeletalMesh = pGuard->Mesh;
                if (!pSkeletalMesh)
                    continue;

                // Draw ESP features
                DrawBox(pSkeletalMesh, pPlayerController, pDrawList, pGuard);
                DrawSkeleton(pSkeletalMesh, pPlayerController, pDrawList);
                DrawDebugSkeleton(pSkeletalMesh, pPlayerController, pDrawList);
            }
        }

        SDK::ULevel* pPersistentLevel = pGWorld->PersistentLevel;
        pPersistentLevel->Actors;

        for (SDK::AActor* pActor : pPersistentLevel->Actors) {
            if (!pActor)
                continue;

            std::vector<SDK::UClass*> actorClasses = {
                SDK::ABP_RFIDTagBase_C::StaticClass(),
                SDK::ABP_KeycardBase_C::StaticClass(),
                SDK::ABP_CarriedInteractableBase_C::StaticClass(),
                SDK::UGE_CarKeys_C::StaticClass(),
                SDK::UGA_Phone_C::StaticClass(),
            };

            if (std::none_of(actorClasses.begin(), actorClasses.end(), [pActor](SDK::UClass* pClass) { return pActor->IsA(pClass); }))
                continue;

            // Drae name on item
            SDK::FVector ActorLocation = pActor->K2_GetActorLocation();
            SDK::FVector2D ScreenLocation;
            if (!pPlayerController->ProjectWorldLocationToScreen(ActorLocation, &ScreenLocation, false))
                continue;

            char szName[64];
            szName[pActor->Class->Name.GetRawString().copy(szName, 63)] = '\0';
            ImVec2 vecTextSize = ImGui::CalcTextSize(szName);
            ImGui::GetBackgroundDrawList()->AddText({ScreenLocation.X - vecTextSize.x / 2, ScreenLocation.Y - 8.f}, IM_COL32(0, 255, 0, 255), szName);
        }
    }

    void RenderDebugESP(SDK::ULevel* pPersistentLevel, SDK::APlayerController* pPlayerController) {
        if (!g_Config.bDebugESP)
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
