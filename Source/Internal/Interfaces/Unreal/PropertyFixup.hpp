#pragma once
#include "FrameworkConfig.hpp"

#if GAME_WINGDK
#include "WinGDK/PropertyFixup.hpp"
#else
#include "Steam/PropertyFixup.hpp"
#endif
