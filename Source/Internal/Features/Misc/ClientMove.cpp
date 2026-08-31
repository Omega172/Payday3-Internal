#include "pch.h"
#include "ClientMove.hpp"

SDK::USBZPlayerMovementComponent* ClientMove::GetMovement(SDK::ASBZPlayerCharacter* pChar)
{
	if (!pChar || !Memory::IsValidObjectPtr(pChar))
		return nullptr;

	auto* pBase = pChar->CharacterMovement;
	if (!pBase || !Memory::IsValidObjectPtr(pBase))
		return nullptr;

	if (!pBase->IsA(SDK::USBZPlayerMovementComponent::StaticClass()))
		return nullptr;

	return reinterpret_cast<SDK::USBZPlayerMovementComponent*>(pBase);
}

void ClientMove::EnableNoclip(SDK::ASBZPlayerCharacter* pChar, SDK::USBZPlayerMovementComponent* pMove)
{
	if (pChar->GetActorEnableCollision())
		pChar->SetActorEnableCollision(false);

	pMove->MovementMode = SDK::EMovementMode::MOVE_Flying;
	pMove->BrakingDecelerationFlying = 10000.f;
	pMove->MaxFlySpeed = 10000.f;

	float flSpeed = m_pSpeed->GetValue();
	m_pFasterHotkey->Update();
	if (m_pFasterHotkey->GetValue())
		flSpeed *= 2.f;

	pMove->Velocity = pMove->GetLastInputVector() * flSpeed;
}

void ClientMove::DisableNoclip(SDK::ASBZPlayerCharacter* pChar, SDK::USBZPlayerMovementComponent* pMove)
{
	if (!pChar || !pMove)
		return;

	pChar->SetActorEnableCollision(true);
	pMove->MovementMode = SDK::EMovementMode::MOVE_Walking;
	pMove->Velocity = SDK::FVector{};
}

void ClientMove::SyncPosition(SDK::ASBZPlayerCharacter* pChar, SDK::USBZPlayerMovementComponent* pMove)
{
	if (!pChar || !pMove)
		return;

	// Fake a zero-length vault so the server accepts our location (online).
	SDK::FVector vecPoint = pChar->K2_GetActorLocation();
	SDK::FSBZMinimalAgilityTraversalTrajectory trajectory{};
	trajectory.JumpPointLocation = vecPoint;
	trajectory.EdgePointFrontLocation = vecPoint;
	trajectory.EdgePointBackLocation = vecPoint;
	trajectory.LandPointLocation = vecPoint;
	trajectory.EntrySpeed = (std::numeric_limits<int16_t>::max)();
	trajectory.AgilityType = SDK::ESBZAgilityTraversalType::VaultLowFast;
	trajectory.bEndsInCrouchState = false;
	trajectory.bEndsFalling = false;

	pMove->Server_StartTraversal(trajectory);
}

void ClientMove::HandleMenu()
{
	static std::once_flag onceflag;

	std::call_once(onceflag, [this]() {
		auto pHeaderGroup = static_cast<HeaderGroup*>(Framework::menu->GetChild("HEADER_GROUP"));
		if (pHeaderGroup)
			pHeaderGroup->AddHeaders(ClientMove::s_iPageId, { "CLIENTMOVE_TAB1"Hashed });

		m_pTab1Left->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() - 20.0f));
		});
		m_pTab1Right->SetCallback([]() {
			return ImVec2((ImGui::GetWindowWidth() - 10.0f - 10.0f * 2) / 2, (ImGui::GetWindowHeight() - 20.0f));
		});

		m_pTab1Left->AddElement(m_pEnabled.get());
		m_pTab1Left->AddElement(m_pAutoSync.get());
		m_pTab1Left->AddElement(m_pSpeed.get());

		m_pTab1Right->AddElement(m_pHotkey.get());
		m_pTab1Right->AddElement(m_pFasterHotkey.get());
		m_pTab1Right->AddElement(m_pSyncHotkey.get());

		m_pTab1Group->AddElement(m_pTab1Left.get());
		m_pTab1Group->AddElement(m_pTab1Right.get());
		m_pTab1Page->AddElement(m_pTab1Group.get());

		Framework::menu->GetChild("HEADER_GROUP")->GetChild("BODY")->AddElement(m_pTab1Page.get());
	});
}

void ClientMove::Run()
{
	m_pHotkey->Update();
	m_pSyncHotkey->Update();

	SDK::ASBZPlayerCharacter* pChar = Unreal::GetLocalASBZPlayerCharacter();
	if (!pChar)
	{
		m_eState = EState::Disabled;
		m_bWasSyncHotkey = false;
		return;
	}

	SDK::USBZPlayerMovementComponent* pMove = GetMovement(pChar);
	if (!pMove)
		return;

	const bool bWant =
		m_pEnabled->GetValue() && m_pHotkey->GetValue();

	if (bWant)
	{
		EnableNoclip(pChar, pMove);
		m_eState = EState::Active;

		const bool bSyncDown = m_pSyncHotkey->GetValue();
		if (bSyncDown && !m_bWasSyncHotkey)
			SyncPosition(pChar, pMove);
		m_bWasSyncHotkey = bSyncDown;
	}
	else
	{
		m_bWasSyncHotkey = false;
		if (m_eState == EState::Active)
		{
			DisableNoclip(pChar, pMove);
			if (m_pAutoSync->GetValue())
				SyncPosition(pChar, pMove);
			m_eState = EState::Disabled;
		}
	}
}

void ClientMove::Destroy()
{
	SDK::ASBZPlayerCharacter* pChar = Unreal::GetLocalASBZPlayerCharacter();
	SDK::USBZPlayerMovementComponent* pMove = pChar ? GetMovement(pChar) : nullptr;
	if (m_eState == EState::Active && pChar && pMove)
		DisableNoclip(pChar, pMove);
	m_eState = EState::Disabled;
}
