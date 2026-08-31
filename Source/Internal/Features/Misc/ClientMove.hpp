#pragma once
#include "pch.h"

// Hold hotkey: collision off + MOVE_Flying. Optional sync traversal RPC for online.
class ClientMove : public BaseFeature
{
private:
	inline static uint8_t s_iPageId = ElementBase::AddPage("CLIENTMOVE_BUTTON"Hashed, ICON_FA_PERSON_WALKING);

	std::unique_ptr<RadioButtonIcon> m_pMenuButton = std::make_unique<RadioButtonIcon>(
		std::string("CLIENTMOVE_BUTTON"),
		"CLIENTMOVE_BUTTON"Hashed,
		ElementBase::Style_t({ .vec2Size = ImVec2(-0.1f, 0) }),
		ICON_FA_PERSON_WALKING,
		s_iPageId);

	std::unique_ptr<Page> m_pTab1Page = std::make_unique<Page>(
		"CLIENTMOVE_TAB1_PAGE",
		ElementBase::Style_t(),
		s_iPageId,
		0);

	std::unique_ptr<Group> m_pTab1Group = std::make_unique<Group>("CLIENTMOVE_TAB1_GROUP", ElementBase::Style_t{});

	std::unique_ptr<GroupChild> m_pTab1Left = std::make_unique<GroupChild>(
		"CLIENTMOVE_TAB1_LEFT",
		"CLIENTMOVE_TAB1_LEFT"Hashed,
		ElementBase::Style_t(),
		ImGuiChildFlags_Border);

	std::unique_ptr<GroupChild> m_pTab1Right = std::make_unique<GroupChild>(
		"CLIENTMOVE_TAB1_RIGHT",
		"CLIENTMOVE_TAB1_RIGHT"Hashed,
		ElementBase::Style_t{ .eSameLine = ElementBase::ESameLine::Same },
		ImGuiChildFlags_Border);

	std::unique_ptr<Checkbox> m_pEnabled = std::make_unique<Checkbox>("CLIENTMOVE_ENABLED", "CLIENTMOVE_ENABLED"Hashed);
	std::unique_ptr<Hotkey> m_pHotkey = std::make_unique<Hotkey>("CLIENTMOVE_HOTKEY", "CLIENTMOVE_HOTKEY"Hashed);
	std::unique_ptr<Hotkey> m_pFasterHotkey = std::make_unique<Hotkey>("CLIENTMOVE_FASTER_HOTKEY", "CLIENTMOVE_FASTER_HOTKEY"Hashed);
	std::unique_ptr<Hotkey> m_pSyncHotkey = std::make_unique<Hotkey>("CLIENTMOVE_SYNC_HOTKEY", "CLIENTMOVE_SYNC_HOTKEY"Hashed);
	std::unique_ptr<Checkbox> m_pAutoSync = std::make_unique<Checkbox>("CLIENTMOVE_AUTO_SYNC", "CLIENTMOVE_AUTO_SYNC"Hashed);
	std::unique_ptr<SliderFloat> m_pSpeed = std::make_unique<SliderFloat>(
		"CLIENTMOVE_SPEED",
		"CLIENTMOVE_SPEED"Hashed,
		ElementBase::Style_t{ .vec2Size = ImVec2(100.f, 0.f) },
		1200.f,
		100.f,
		5000.f,
		"%.0f");

	enum class EState : uint8_t
	{
		Disabled,
		Active,
	};

	EState m_eState = EState::Disabled;
	bool m_bWasSyncHotkey = false;

	SDK::USBZPlayerMovementComponent* GetMovement(SDK::ASBZPlayerCharacter* pChar);
	void EnableNoclip(SDK::ASBZPlayerCharacter* pChar, SDK::USBZPlayerMovementComponent* pMove);
	void DisableNoclip(SDK::ASBZPlayerCharacter* pChar, SDK::USBZPlayerMovementComponent* pMove);
	void SyncPosition(SDK::ASBZPlayerCharacter* pChar, SDK::USBZPlayerMovementComponent* pMove);

public:
	void HandleMenu() override;
	void Run() override;
	void Destroy() override;
	RadioButtonIcon* GetMenuButton() const { return m_pMenuButton.get(); }
	std::string GetName() override { return "ClientMove"; };
};

inline std::unique_ptr<ClientMove> pClientMove = std::make_unique<ClientMove>();
