#pragma once
#pragma execution_character_set("utf-8")

// Identity/versioning macros that both Shared and Internal/Proxy code need at compile time.
// Engine selection (ENGINE_UNREAL/UNITY/OTHER) and renderer selection live in Internal's own
// FrameworkConfig.hpp, since Shared has no opinion on how it's being hosted.

#define STRR(X) #X
#define STR(X) STRR(X)

#define FRAMEWORK_MAJOR_VERSION 3
#define FRAMEWORK_MINOR_VERSION 2
#define FRAMEWORK_REWORK_VERSION 1
#define FRAMEWORK_VERSION FRAMEWORK_MAJOR_VERSION.FRAMEWORK_MINOR_VERSION.FRAMEWORK_REWORK_VERSION

#ifndef GAME_WINGDK
#define GAME_WINGDK 0 // 0 for Steam, 1 for WinGDK
#endif

#ifndef GAME_STEAM
#define GAME_STEAM (!GAME_WINGDK)
#endif

#define FRAMEWORK_CODENAME "OmegaWare"

#ifndef TARGET_GAME_NAME
#if GAME_WINGDK
#define TARGET_GAME_NAME "PAYDAY3-WinGDK-Shipping"
#else
#define TARGET_GAME_NAME "PAYDAY3-Win64-Shipping"
#endif
#endif

#pragma warning( push ) // disable "operator '!=': deprecated for array types" warning
#pragma warning( disable : 5056)
static_assert(TARGET_GAME_NAME != "", "Target game not set, this HAS to be set or it fucks up the logging system, the console, the menu, and the config system.");
#pragma warning( pop )

