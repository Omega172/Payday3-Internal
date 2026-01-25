add_rules("mode.debug", "mode.release")

if is_mode("debug") then
    set_symbols("debug", "edit")
end

set_targetdir(is_mode("debug") and "Build/Debug" or "Build/Release")
set_runtimes(is_mode("debug") and "MDd" or "MD")

add_requires("vcpkg::minhook 1.3.4")
add_requires("vcpkg::imgui", {configs = {vs_runtimes = "MDd",features = {"win32-binding", "dx12-binding"}}})

target("Payday3-Internal")
    set_languages("c++latest")
    set_kind("shared")
    add_files("Source/Payday3-Internal/DLLMain.cpp")
    add_files("Source/Payday3-Internal/Utils/**.ixx")
    add_files("Source/Payday3-Internal/Hook/**.ixx")
    add_files("Source/Payday3-Internal/Menu/**.ixx")
    add_files("Source/Payday3-Internal/Features/**.ixx")
    add_files("Source/Payday3-Internal/Features/**.cpp");
    add_files("Source/Payday3-Internal/Dumper-7/SDK/Basic.cpp")
    add_files("Source/Payday3-Internal/Dumper-7/SDK/CoreUObject_functions.cpp")
    add_files("Source/Payday3-Internal/Dumper-7/SDK/Engine_Functions.cpp")
    add_files("Source/Payday3-Internal/Dumper-7/SDK/Starbreeze_functions.cpp")
    add_files("Source/Payday3-Internal/Dumper-7/SDK/SBZWorldRuntime_functions.cpp")
    add_files("Source/Payday3-Internal/Dumper-7/SDK/GameplayAbilities_functions.cpp")

    add_packages("vcpkg::minhook", "vcpkg::imgui")
    add_syslinks("d3d12", "dxgi")

    add_links("user32", "minhook.x64", "imgui")
