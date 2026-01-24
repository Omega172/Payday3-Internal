module;
#include <imgui.h>
#include "../Dumper-7/SDK.hpp"

export module Menu;
import Utils.Logging;
import Features.ESP;

export namespace Menu
{
    void PreDraw()
    {
        SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
        if (!pGWorld)
            return;

        SDK::UGameInstance* pGameInstance = pGWorld->OwningGameInstance;
        if (!pGameInstance)
            return;

        SDK::ULocalPlayer* pLocalPlayer = pGameInstance->LocalPlayers[0];
        if (!pLocalPlayer)
            return;

        SDK::APlayerController* pPlayerController = pLocalPlayer->PlayerController;
        if (!pPlayerController)
            return;

        SDK::ULevel* pPersistentLevel = pGWorld->PersistentLevel;
        if (!pPersistentLevel)
            return;

        ESP::Render(pGWorld, pPlayerController);
        ESP::RenderDebugESP(pPersistentLevel, pPlayerController);
    }

    void DrawEnemyESPSection(const char* szType, ESP::EnemyESP& stSettings)
    {
        if(!ImGui::BeginCombo(szType, std::format("{}###{}", stSettings.m_sPreviewText, szType).c_str()))
            return;

        if (ImGui::Selectable("Box", &stSettings.m_bBox) ||
            ImGui::Selectable("Health", &stSettings.m_bHealth) ||
            ImGui::Selectable("Armor", &stSettings.m_bArmor) ||
            ImGui::Selectable("Name", &stSettings.m_bName) ||
            ImGui::Selectable("Flags", &stSettings.m_bFlags) ||
            ImGui::Selectable("Skeleton", &stSettings.m_bSkeleton) ||
            ImGui::Selectable("Outline", &stSettings.m_bOutline))
            stSettings.UpdatePreviewText();

        ImGui::EndCombo();
    }

	// Draw the main menu content
	void Draw(bool& bShowMenu)
	{
		if (!bShowMenu)
			return;

        auto& espConfig = ESP::GetConfig();

        ImGui::Begin("Payday 3 Internal", &bShowMenu, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
		// Header
		ImGui::Text("DirectX 12 Hook Active");
		ImGui::Text("Press INSERT to toggle menu");
		ImGui::Separator();
		
		// Performance metrics
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
			1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		
		ImGui::Separator();
		
		ImGui::Checkbox("Enable ESP", &espConfig.bESP);
        if (espConfig.bESP) {
            ImGui::Indent();

            DrawEnemyESPSection("Normal Enemies", espConfig.m_stNormalEnemies);
            DrawEnemyESPSection("Special Enemies", espConfig.m_stSpecialEnemies);

#ifdef _DEBUG
            ImGui::Checkbox("Debug Draw Bone Indices", &espConfig.bDebugDrawBoneIndices);
            ImGui::Checkbox("Debug Draw Bone Names Instead of Indices", &espConfig.bDebugDrawBoneNames);
#endif
            
#ifdef _DEBUG
            ImGui::Checkbox("Debug Skeleton", &espConfig.bDebugSkeleton);
            if (espConfig.bDebugSkeleton) {
                ImGui::Indent();
                ImGui::Checkbox("Debug Skeleton Draw Bone Indices", &espConfig.bDebugSkeletonDrawBoneIndices);
                ImGui::Checkbox("Debug Skeleton Draw Bone Names Instead of Indices", &espConfig.bDebugSkeletonDrawBoneNames);
                ImGui::Unindent();
            }
#endif
            ImGui::Unindent();
        }
#ifdef _DEBUG
        ImGui::Checkbox("Debug ESP (Show Class Names)", &espConfig.bDebugESP);
#endif

        ImGui::End();
	}

    void PostDraw() {}
}
