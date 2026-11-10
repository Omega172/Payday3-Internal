#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if OMEGAWARE_ENGINE_UNREAL
#pragma warning(push)
#pragma warning(disable : 4005 4369 4309 4267 4244)
#include "SDK/Engine_classes.hpp"
#include "SDK/Starbreeze_classes.hpp"
#pragma warning(pop)
#endif
