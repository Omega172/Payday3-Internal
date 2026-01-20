module;
#include <imgui.h>
#include "../Dumper-7/SDK.hpp"

export module Menu;
import Utils.Logging;
static bool g_bESP = false;
static bool g_bDebugESP = false;
export namespace Menu
{
    void ESP(SDK::UWorld* pGWorld, SDK::APlayerController* pPlayerController) {
        if (!g_bESP)
            return;

        SDK::USBZWorldRuntime* pWorldRuntime = reinterpret_cast<SDK::USBZWorldRuntime*>(SDK::USBZWorldRuntime::GetWorldRuntime(pGWorld));
        if (pWorldRuntime) {
            UC::TArray<SDK::UObject*> guards = pWorldRuntime->AllAliveAIGuards->Objects;
            for (int i = 0; i < guards.Num(); i++) {
                auto pGaurd = reinterpret_cast<SDK::ACH_BaseCop_C*>(guards[i]);
                SDK::FVector2D vec2ScreenLocation;
                if (!pGaurd || !pPlayerController->ProjectWorldLocationToScreen(pGaurd->K2_GetActorLocation(), &vec2ScreenLocation, false))
                    continue;

                pGaurd->Multicast_SetMarked(true);
            }
        }
    }

    void DebugESP(SDK::ULevel* pPersistentLevel, SDK::APlayerController* pPlayerController) {
        if (!g_bDebugESP)
            return;

        ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

        for (SDK::AActor* pActor : pPersistentLevel->Actors) {
            if (!pActor)
                continue;

            SDK::FVector ActorLocation = pActor->K2_GetActorLocation();
            SDK::FVector2D ScreenLocation;

            if (!pPlayerController->ProjectWorldLocationToScreen(ActorLocation, &ScreenLocation, false))
                continue;

            if (!g_bDebugESP)
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

        ESP(pGWorld, pPlayerController);

        DebugESP(pPersistentLevel, pPlayerController);
    }

	// Draw the main menu content
	void Draw(bool& bShowMenu)
	{
		if (!bShowMenu)
			return;

        ImGui::Begin("Payday 3 Internal", &bShowMenu, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
		// Header
		ImGui::Text("DirectX 12 Hook Active");
		ImGui::Text("Press INSERT to toggle menu");
		ImGui::Separator();
		
		// Performance metrics
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
			1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		
		ImGui::Separator();
		
		ImGui::Checkbox("Enable ESP", &g_bESP);
        if (g_bESP)
            ImGui::Checkbox("Debug ESP (Show Class Names)", &g_bDebugESP);

        ImGui::End();
	}

    void PostDraw() {}
}
