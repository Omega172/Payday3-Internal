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
            ImGui::Checkbox("Box ESP", &espConfig.bBoxESP);
            ImGui::Checkbox("Skeleton ESP", &espConfig.bSkeleton);
            if (espConfig.bSkeleton) {
                ImGui::Indent();
#ifdef _DEBUG
                ImGui::Checkbox("Debug Draw Bone Indices", &espConfig.bDebugDrawBoneIndices);
                ImGui::Checkbox("Debug Draw Bone Names Instead of Indices", &espConfig.bDebugDrawBoneNames);
#endif
                ImGui::Unindent();
            }
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
