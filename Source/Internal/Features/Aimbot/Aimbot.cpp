#include "pch.h"
#include "Aimbot.hpp"
#include "TargetingHelpers.hpp"

namespace
{
	float NormalizeAngle(float angle)
	{
		while (angle > 180.0f)
			angle -= 360.0f;
		while (angle < -180.0f)
			angle += 360.0f;
		return angle;
	}
}

int Aimbot::CurrentAimbotType()
{
	return m_pAimbotType->GetSelectedIndex(); // 0 = Silent, 1 = Snapping
}

void Aimbot::HandleMenu()
{
	static std::once_flag onceflag;

	std::call_once(onceflag, [this]() {
		auto pHeaderGroup = static_cast<HeaderGroup*>(Framework::menu->GetChild("HEADER_GROUP"));

		if (pHeaderGroup)
			pHeaderGroup->AddHeaders(Aimbot::s_iAimbotPageId, { "AIMBOT_TAB1"Hashed });

		m_pTab1Left->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() - 20.0f));
		});
		m_pTab1Right->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() - 20.0f));
		});

		m_pAimbotType->AddOption("Silent");
		m_pAimbotType->AddOption("Snapping");

		m_pTab1Left->AddElement(m_pAimbotEnabled.get());
		m_pTab1Left->AddElement(m_pAimbotVisibleCheck.get());
		m_pTab1Left->AddElement(m_pAimbotFOVEnabled.get());
		m_pTab1Right->AddElement(m_pAimbotHotkey.get()); // key defaults unbound, bind it via the UI
		m_pTab1Right->AddElement(m_pAimbotType.get());
		m_pTab1Right->AddElement(m_pAimbotTarget.get());
		m_pAimbotTarget->AddOption("Head");
		m_pAimbotTarget->AddOption("Torso");
		m_pAimbotTarget->AddOption("Left Arm");
		m_pAimbotTarget->AddOption("Right Arm");
		m_pAimbotTarget->AddOption("Hips");
		m_pAimbotTarget->AddOption("Left Leg");
		m_pAimbotTarget->AddOption("Right Leg");
		m_pAimbotTarget->SetSelectedIndex(0);
		m_pTab1Right->AddElement(m_pAimbotFOVColor.get());
		m_pAimbotFOVColor->SetValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
		m_pTab1Right->AddElement(m_pFOV.get());
		m_pTab1Right->AddElement(m_pSmoothing.get());

		m_pTab1Group->AddElement(m_pTab1Left.get());
		m_pTab1Group->AddElement(m_pTab1Right.get());

		m_pTab1Page->AddElement(m_pTab1Group.get());

		Framework::menu->GetChild("HEADER_GROUP")->GetChild("BODY")->AddElement(m_pTab1Page.get());
	});
}

void Aimbot::UpdateAim()
{
	auto ClearOverride = [this]() {
		ShouldOverrideView = false;
	};

	if (!m_pAimbotEnabled->GetValue())
	{
		ClearOverride();
		return;
	}

	m_pAimbotHotkey->Update();
	const bool bHotkeyOk = m_pAimbotHotkey->GetValue() || (m_pAimbotHotkey->GetKey() == ImGuiKey_None);
	if (!bHotkeyOk)
	{
		ClearOverride();
		return;
	}

	SDK::UWorld* pWorld = SDK::UWorld::GetWorld();
	if (!pWorld)
	{
		ClearOverride();
		return;
	}

	SDK::APlayerController* pPlayerController = SDK::UGameplayStatics::GetPlayerController(pWorld, 0);
	if (!pPlayerController || !pPlayerController->PlayerCameraManager)
	{
		ClearOverride();
		return;
	}

	SDK::FVector vecCameraLocation = pPlayerController->PlayerCameraManager->GetCameraLocation();
	SDK::FRotator rotCameraRotation = pPlayerController->PlayerCameraManager->GetCameraRotation();

	const float maxScreenDistance = m_pAimbotFOVEnabled->GetValue() ? static_cast<float>(m_pFOV->GetValue()) : std::numeric_limits<float>::max();
	const bool bVisibleCheck = m_pAimbotVisibleCheck->GetValue();

	const wchar_t* targetPart =
		m_pAimbotTarget->GetSelectedIndex() == 0 ? L"Head" :
		m_pAimbotTarget->GetSelectedIndex() == 1 ? L"Spine3" :
		m_pAimbotTarget->GetSelectedIndex() == 2 ? L"LeftForeArm" :
		m_pAimbotTarget->GetSelectedIndex() == 3 ? L"RightForeArm" :
		m_pAimbotTarget->GetSelectedIndex() == 4 ? L"Hips" :
		m_pAimbotTarget->GetSelectedIndex() == 5 ? L"LeftLeg" :
		m_pAimbotTarget->GetSelectedIndex() == 6 ? L"RightLeg" : L"Head";

	auto optTarget = TargetingHelpers::SelectBestTarget(pWorld, pPlayerController, true, false, maxScreenDistance, bVisibleCheck, targetPart);
	if (!optTarget.has_value())
	{
		ClearOverride();
		return;
	}

	const SDK::FRotator desiredRotation = SDK::UKismetMathLibrary::FindLookAtRotation(
		vecCameraLocation,
		optTarget->AimWorldLocation
	);

	const float smoothing = std::max(1, m_pSmoothing->GetValue());
	const float alpha = 1.0f / smoothing;

	const float deltaYaw = NormalizeAngle(desiredRotation.Yaw - rotCameraRotation.Yaw) * alpha;
	const float deltaPitch = NormalizeAngle(desiredRotation.Pitch - rotCameraRotation.Pitch) * alpha;

	if (CurrentAimbotType() == 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		ShouldOverrideView = true;
		OverrideLocation = optTarget->AimWorldLocation;
		OverrideRotation = desiredRotation;
	}
	else if (CurrentAimbotType() == 1)
	{
		ShouldOverrideView = false;
		pPlayerController->AddYawInput(deltaYaw);
		pPlayerController->AddPitchInput(-deltaPitch);
	}
}

void Aimbot::Render()
{
	if (m_pAimbotFOVEnabled->GetValue())
	{
		const ImU32 color = ImGui::ColorConvertFloat4ToU32(m_pAimbotFOVColor->GetValue());
		const float maxScreenDistance = static_cast<float>(m_pFOV->GetValue());

		ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();
		const ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
		pDrawList->AddCircle(center, maxScreenDistance, color, 100, 1.0f);
	}

	UpdateAim();
}

void Aimbot::Run()
{
}
