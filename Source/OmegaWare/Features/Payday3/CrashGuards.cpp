#include "Features/Payday3/Payday3.hpp"

#if OMEGAWARE_ENGINE_UNREAL

#include "Core/Log.hpp"
#include "Features/Feature.hpp"
#include "Hooks/Hook.hpp"
#include "Memory/Memory.hpp"
#include "Memory/Pattern.hpp"
#include "Memory/Safety.hpp"

#include "OmegaRenderHook/Core/Guarded.hpp"

#include "OmegaGUI/OmegaGUI.hpp"
#include "OmegaLocalization/OmegaLocalization.hpp"

#include <Windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <format>
#include <optional>

namespace OmegaWare
{
	namespace
	{
		using FnAbilityGuard = void (*)(SDK::ASBZCharacter*, void*);
		using FnBrainGuard = void (*)(void*, SDK::UBrainComponent*, void*);

		Hooks::clHook<FnAbilityGuard> g_clAbilityGuard("ai ability guard");
		Hooks::clHook<FnBrainGuard> g_clBrainGuard("ai brain guard");
		using FnChanceGuard = float (*)(void*);
		Hooks::clHook<FnChanceGuard> g_clChanceGuard("ai chance guard");
		std::atomic<std::uint32_t> g_uChanceSkips = 0;
		using FnScoreGuard = float (*)(void*);
		Hooks::clHook<FnScoreGuard> g_clScoreGuard("ai scoring guard");
		std::atomic<std::uint32_t> g_uScoreSkips = 0;
		using FnNullThisGuard = std::uint8_t (*)(void*);
		Hooks::clHook<FnNullThisGuard> g_clNullThisGuard("ai null object guard");
		using FnPawnGuard = void (*)(void*);
		Hooks::clHook<FnPawnGuard> g_clPawnGuard("ai pawn action guard");
		std::atomic<std::uint32_t> g_uPawnSkips = 0;
		using FnTickGuard = void (*)(void*, float);
		Hooks::clHook<FnTickGuard> g_clTickGuard("mission end tick guard");
		std::atomic<std::uint32_t> g_uTickSkips = 0;
		using FnPureCall = int (*)();
		Hooks::clHook<FnPureCall> g_clPureCall("pure virtual guard");
		std::atomic<std::uint32_t> g_uPureCalls = 0;
		std::atomic<std::uint32_t> g_uAbilitySkips = 0;
		std::atomic<std::uint32_t> g_uBrainSkips = 0;

		void NoteSkip(std::atomic<std::uint32_t>& rCounter, std::string_view strWhat)
		{
			const std::uint32_t uCount = rCounter.fetch_add(1, std::memory_order_relaxed) + 1;
			if (uCount == 1 || uCount == 10 || uCount == 100 || uCount % 1000 == 0)
				LogWarning("crash guard: skipped {} ({} so far)", strWhat, uCount);
		}

		int HkPureCall()
		{
			Hooks::clCallGuard clGuard;
			const std::uint32_t uCount = g_uPureCalls.fetch_add(1, std::memory_order_relaxed) + 1;
			if (uCount <= 10 || uCount % 1000 == 0)
			{
				const std::optional<Memory::stModule> optModule = Memory::GetModule();
				const auto uCaller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
				LogWarning("crash guard: skipped a pure virtual call from game code at +{:#x} ({} so far)", optModule ? uCaller - optModule->uBase : uCaller, uCount);
			}
			return 0;
		}

		void HkAbilityGuard(SDK::ASBZCharacter* pCharacter, void* pArgument)
		{
			Hooks::clCallGuard clGuard;
			if (!pCharacter)
			{
				NoteSkip(g_uAbilitySkips, "an ability system read on an AI without a pawn");
				return;
			}
			g_clAbilityGuard(pCharacter, pArgument);
		}

		void HkBrainGuard(void* pThis, SDK::UBrainComponent* pBrain, void* pArgument)
		{
			Hooks::clCallGuard clGuard;
			SDK::AAIController* pOwner = pBrain ? pBrain->AIOwner : nullptr;
			if (!pOwner || !Memory::IsValidObjectPtr(pOwner) || !pOwner->Pawn)
			{
				NoteSkip(g_uBrainSkips, "a brain tick on an AI without a pawn");
				return;
			}
			g_clBrainGuard(pThis, pBrain, pArgument);
		}

		float HkChanceGuard(void* pThis)
		{
			Hooks::clCallGuard clGuard;
			const auto pBase = reinterpret_cast<const std::uint8_t*>(pThis);
			const std::uint8_t* pOwner = nullptr;
			const std::uint8_t* pPawn = nullptr;
			bool bValid = Memory::IsReadable(pBase + 0x58, sizeof(void*));
			if (bValid)
			{
				pOwner = *reinterpret_cast<const std::uint8_t* const*>(pBase + 0x58);
				bValid = pOwner && Memory::IsReadable(pOwner + 0x370, sizeof(void*));
			}
			if (bValid)
			{
				pPawn = *reinterpret_cast<const std::uint8_t* const*>(pOwner + 0x370);
				bValid = pPawn != nullptr;
			}
			if (!bValid)
			{
				NoteSkip(g_uChanceSkips, "a chance roll on an AI without a pawn");
				return 0.f;
			}
			return g_clChanceGuard(pThis);
		}

		float HkScoreGuard(void* pThis)
		{
			Hooks::clCallGuard clGuard;
			if (!pThis)
			{
				NoteSkip(g_uScoreSkips, "an ai scoring call on an AI without a pawn");
				return 0.f;
			}
			float flScore = 0.f;
			auto fnCall = [&] { flScore = g_clScoreGuard(pThis); };
			OmegaRenderHook::stFault stFault;
			if (OmegaRenderHook::InvokeGuarded(fnCall, stFault))
				return flScore;
			NoteSkip(g_uScoreSkips, "an ai scoring call that faulted on an AI without a pawn");
			return 0.f;
		}

		std::uint8_t HkNullThisGuard(void* pThis)
		{
			Hooks::clCallGuard clGuard;
			if (!pThis)
			{
				NoteSkip(g_uScoreSkips, "an ai check on a missing object on an AI without a pawn");
				return 0;
			}
			return g_clNullThisGuard(pThis);
		}

		void HkPawnGuard(void* pThis)
		{
			Hooks::clCallGuard clGuard;
			const auto pBase = reinterpret_cast<const std::uint8_t*>(pThis);
			const std::uint8_t* pOwner = nullptr;
			bool bValid = pThis && Memory::IsReadable(pBase + 0x118, sizeof(void*));
			if (bValid)
			{
				pOwner = *reinterpret_cast<const std::uint8_t* const*>(pBase + 0x118);
				bValid = pOwner && Memory::IsReadable(pOwner + 0x370, sizeof(void*));
			}
			if (bValid)
				bValid = *reinterpret_cast<const std::uint8_t* const*>(pOwner + 0x370) != nullptr;
			if (!bValid)
			{
				NoteSkip(g_uPawnSkips, "an ai action while its pawn was missing");
				return;
			}
			auto fnCall = [&] { g_clPawnGuard(pThis); };
			OmegaRenderHook::stFault stFault;
			if (!OmegaRenderHook::InvokeGuarded(fnCall, stFault))
				NoteSkip(g_uPawnSkips, "an ai pawn action that faulted");
		}

		void HkTickGuard(void* pThis, float flDelta)
		{
			Hooks::clCallGuard clGuard;
			if (!pThis)
			{
				NoteSkip(g_uTickSkips, "a tick on a missing object");
				return;
			}
			auto fnCall = [&] { g_clTickGuard(pThis, flDelta); };
			OmegaRenderHook::stFault stFault;
			if (!OmegaRenderHook::InvokeGuarded(fnCall, stFault))
				NoteSkip(g_uTickSkips, "a per frame update that faulted while the mission was ending");
		}

		[[nodiscard]] std::uintptr_t FindUnique(const auto& rPattern)
		{
			const std::optional<Memory::stModule> optModule = Memory::GetModule();
			if (!optModule)
				return 0;
			const std::vector<std::uintptr_t> aMatches = Memory::FindAllPatterns(optModule->uBase, optModule->uSize, rPattern);
			return aMatches.size() == 1 ? aMatches.front() : 0;
		}

		[[nodiscard]] std::uintptr_t FindScoreGuardTarget()
		{
			constexpr auto aPattern = Memory::Pattern<"48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 48 83 EC 50 0F 29 70 E8 48 8B F1 0F 29 78 D8 44 0F 29 40 C8 48 8B 01 FF 90 00 03 00 00 F3 0F 10 3D ? ? ? ? 0F 28 F0">;
			return FindUnique(aPattern);
		}

		[[nodiscard]] std::uintptr_t FindPawnGuardTarget()
		{
			constexpr auto aPattern = Memory::Pattern<"48 89 5C 24 08 57 48 83 EC 40 48 8B D9 E8 ? ? ? ? 48 8B 83 18 01 00 00 48 8B CB 48 8B B8 70 03 00 00 E8 ? ? ? ? 48 85 C0 74 14 48 8B D7">;
			return FindUnique(aPattern);
		}

		[[nodiscard]] std::uintptr_t FindTickGuardTarget()
		{
			constexpr auto aPattern = Memory::Pattern<"48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 A8 FE FF FF 48 81 EC 20 02 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 44 0F 29 90 78 FF FF FF 44 0F 29 98 68 FF FF FF 44 0F 29 A0 58 FF FF FF 44 0F 29 A8 48 FF FF FF 44 0F 29 B0 38 FF FF FF 44 0F 29 B8 28 FF FF FF 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 70 4C 8B">;
			return FindUnique(aPattern);
		}

		[[nodiscard]] std::uintptr_t FindNullThisGuardTarget()
		{
			constexpr auto aPattern = Memory::Pattern<"48 89 5C 24 08 57 48 83 EC 20 83 B9 10 0E 00 00 FF 48 8B F9 75 04 32 C0 EB 2A 48 8B 01 FF 90 C0 0F 00 00 48 8B CF 8A D8 E8 ? ? ? ? 8A C8 33 C0 84 DB 74 0F 84 C9 74 09 83 BF 90 0C 00 00 FF 75 02 B0 02">;
			return FindUnique(aPattern);
		}

		[[nodiscard]] std::uintptr_t FindChanceGuardTarget()
		{
			constexpr auto aPattern = Memory::Pattern<"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 83 79 68 00 48 8B D9 0F 84 8A 00 00 00 FF 15 ? ? ? ? 25 FF 7F 00 00 66 0F 6E C0 0F 5B C0 F3 0F 59 05 ? ? ? ? 0F 2F 43 60 77 6A 48 8B 43 58 48 8B B0 70 03 00 00 48 8D 8E 50 07 00 00">;
			const std::optional<Memory::stModule> optModule = Memory::GetModule();
			if (!optModule)
				return 0;
			const std::vector<std::uintptr_t> aMatches = Memory::FindAllPatterns(optModule->uBase, optModule->uSize, aPattern);
			return aMatches.size() == 1 ? aMatches.front() : 0;
		}

		[[nodiscard]] std::uintptr_t FindAbilityGuardTarget()
		{
			constexpr auto aPattern = Memory::Pattern<"48 83 EC 28 48 8B 89 68 0C 00 00 48 85 C9 74 0B 83 7A 08 00 74 05 E8 ? ? ? ? 48 83 C4 28 C3">;
			const std::optional<Memory::stModule> optModule = Memory::GetModule();
			if (!optModule)
				return 0;
			const std::vector<std::uintptr_t> aMatches = Memory::FindAllPatterns(optModule->uBase, optModule->uSize, aPattern);
			return aMatches.size() == 1 ? aMatches.front() : 0;
		}

		[[nodiscard]] std::uintptr_t FindBrainGuardTarget()
		{
			constexpr auto aPattern = Memory::Pattern<"48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 48 8D 68 A1 48 81 EC 90 00 00 00 0F 29 70 E8 49 8B F0 48 8B 01 48 8B FA 48 8B D9 FF 90 A0 01 00 00 48 8B C8 E8 ? ? ? ? 80 BB 00 01 00 00 00">;
			const std::optional<Memory::stModule> optModule = Memory::GetModule();
			if (!optModule)
				return 0;
			const std::vector<std::uintptr_t> aMatches = Memory::FindAllPatterns(optModule->uBase, optModule->uSize, aPattern);
			return aMatches.size() == 1 ? aMatches.front() : 0;
		}

		class clCrashGuardsFeature final : public clFeature
		{
		public:
			[[nodiscard]] std::string_view GetName() const override { return "crashguards"; }
			[[nodiscard]] std::string_view GetGroup() const override { return "debug"; }

			bool Setup() override
			{
				if (const std::uintptr_t uAbility = FindAbilityGuardTarget())
					g_clAbilityGuard.Install(uAbility, &HkAbilityGuard);
				else
					LogWarning("crash guard: the ai ability read was not found in this game build, it is not guarded");
				if (const std::uintptr_t uBrain = FindBrainGuardTarget())
					g_clBrainGuard.Install(uBrain, &HkBrainGuard);
				else
					LogWarning("crash guard: the ai brain tick was not found in this game build, it is not guarded");
				if (const std::uintptr_t uChance = FindChanceGuardTarget())
					g_clChanceGuard.Install(uChance, &HkChanceGuard);
				else
					LogWarning("crash guard: the ai chance roll was not found in this game build, it is not guarded");
				if (const std::uintptr_t uScore = FindScoreGuardTarget())
					g_clScoreGuard.Install(uScore, &HkScoreGuard);
				else
					LogWarning("crash guard: the ai scoring call was not found in this game build, it is not guarded");
				if (const std::uintptr_t uNull = FindNullThisGuardTarget())
					g_clNullThisGuard.Install(uNull, &HkNullThisGuard);
				else
					LogWarning("crash guard: the ai object check was not found in this game build, it is not guarded");
				if (const std::uintptr_t uPawn = FindPawnGuardTarget())
					g_clPawnGuard.Install(uPawn, &HkPawnGuard);
				else
					LogWarning("crash guard: the ai pawn action was not found in this game build, it is not guarded");
				if (const std::uintptr_t uTick = FindTickGuardTarget())
					g_clTickGuard.Install(uTick, &HkTickGuard);
				else
					LogWarning("crash guard: the per frame update crashing at mission end was not found in this game build, it is not guarded");
				const HMODULE hRuntime = GetModuleHandleW(L"vcruntime140.dll");
				if (void* pPureCall = hRuntime ? reinterpret_cast<void*>(GetProcAddress(hRuntime, "_purecall")) : nullptr)
					g_clPureCall.Install(pPureCall, &HkPureCall);
				else
					LogWarning("crash guard: the game does not use vcruntime140, pure virtual calls are not guarded");
				return true;
			}

			void Destroy() override
			{
				g_clPureCall.Remove();
				g_clAbilityGuard.Remove();
				g_clBrainGuard.Remove();
				g_clChanceGuard.Remove();
				g_clScoreGuard.Remove();
				g_clNullThisGuard.Remove();
				g_clPawnGuard.Remove();
				g_clTickGuard.Remove();
			}

			void DrawMenu() override
			{
				using namespace OmegaLocalization::Literals;
				if (OmegaGUI::BeginSection("crashguards.section"_l))
				{
					OmegaGUI::TextMuted("crashguards.always_on"_t);
					OmegaGUI::TextMuted(std::format("{}: {} ({})", "crashguards.ability"_t, Hooks::ToString(g_clAbilityGuard.GetStatus()), g_uAbilitySkips.load()));
					OmegaGUI::TextMuted(std::format("{}: {} ({})", "crashguards.brain"_t, Hooks::ToString(g_clBrainGuard.GetStatus()), g_uBrainSkips.load()));
					OmegaGUI::TextMuted(std::format("{}: {} ({})", "crashguards.chance"_t, Hooks::ToString(g_clChanceGuard.GetStatus()), g_uChanceSkips.load()));
					OmegaGUI::TextMuted(std::format("{}: {} ({})", "crashguards.scoring"_t, Hooks::ToString(g_clScoreGuard.GetStatus()), g_uScoreSkips.load()));
					OmegaGUI::TextMuted(std::format("{}: {}", "crashguards.nullobject"_t, Hooks::ToString(g_clNullThisGuard.GetStatus())));
					OmegaGUI::TextMuted(std::format("{}: {} ({})", "crashguards.pawnaction"_t, Hooks::ToString(g_clPawnGuard.GetStatus()), g_uPawnSkips.load()));
					OmegaGUI::TextMuted(std::format("{}: {} ({})", "crashguards.endtick"_t, Hooks::ToString(g_clTickGuard.GetStatus()), g_uTickSkips.load()));
					OmegaGUI::TextMuted(std::format("{}: {} ({})", "crashguards.purecall"_t, Hooks::ToString(g_clPureCall.GetStatus()), g_uPureCalls.load()));
					OmegaGUI::EndSection();
				}
			}
		};

		clCrashGuardsFeature g_clCrashGuardsFeature;
	}
}

#endif
