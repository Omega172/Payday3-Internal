module;
#include <imgui.h>
#include "../Dumper-7/SDK.hpp"

export module Menu;
import Utils.Logging;
static bool g_bESP = false;
static bool g_bDebugESP = false;
static bool g_bExcludeCivilians = true;

export namespace Menu
{
    void PreDraw()
    {
        if (!g_bESP)
            return;

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

        ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();
        if (!pDrawList)
            return;

        for (SDK::AActor* pActor : pPersistentLevel->Actors) {
            if (!pActor)
                continue;

            SDK::FVector ActorLocation = pActor->K2_GetActorLocation();
            SDK::FVector2D ScreenLocation;

            if (!pPlayerController->ProjectWorldLocationToScreen(ActorLocation, &ScreenLocation, false))
                continue;

            if (pActor->IsA(SDK::ACH_BaseCop_C::StaticClass())) {
                SDK::ACH_BaseCop_C* pCop = static_cast<SDK::ACH_BaseCop_C*>(pActor);
                pCop->Multicast_SetMarked(true);
                continue;
            }

            if (pActor->IsA(SDK::ACH_BaseCivilian_C::StaticClass()) || pActor->IsA(SDK::ABP_CivilianController_C::StaticClass())) {
                continue;
            }

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

	// Draw the main menu content
	void Draw(bool& bShowMenu)
	{
		if (!bShowMenu)
			return;

        ImGui::Begin("Payday 3 Internal", &bShowMenu);
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
        if (g_bDebugESP)
            ImGui::Checkbox("Exclude Civilians", &g_bExcludeCivilians);

        ImGui::End();
	}

    void PostDraw() {}
}
