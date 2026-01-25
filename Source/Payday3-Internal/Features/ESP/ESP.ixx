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

export namespace ESP
{
    // ESP Configuration
    
    struct EnemyESP{
        bool m_bBox = false;
        bool m_bHealth = false;
        bool m_bArmor = false;
        bool m_bName = false;
        bool m_bFlags = false;
        bool m_bSkeleton = false;
        bool m_bOutline = false;
        bool m_bWasOutlineActive = true;
        std::string m_sPreviewText = "None";

        void UpdatePreviewText();
    };

    struct Config {
        bool bESP = false;

        EnemyESP m_stNormalEnemies{};
        EnemyESP m_stSpecialEnemies{};

        bool bDebugSkeleton = false;
        bool bDebugDrawBoneIndices = false;
        bool bDebugDrawBoneNames = false;
        bool bDebugSkeletonDrawBoneIndices = false;
        bool bDebugSkeletonDrawBoneNames = false;
        bool bDebugESP = false;
    };

    inline Config& GetConfig() {
        static Config config{};
        return config;
    }

    void Render(SDK::UWorld* pGWorld, SDK::APlayerController* pPlayerController);
    void RenderDebugESP(SDK::ULevel* pPersistentLevel, SDK::APlayerController* pPlayerController);
}
