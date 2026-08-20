add_rules("mode.debug", "mode.release")

if is_mode("debug") then
    set_symbols("debug", "edit")
end

option("avx2")
    set_default(true)
    set_showmenu(true)
    set_description("Enable AVX2 optimizations")
option_end()

option("unreal")
    set_default(true)
    set_showmenu(true)
    set_description("Enable compilation of the Unreal target interface")
option_end()

option("unity")
    set_default(false)
    set_showmenu(true)
    set_description("Enable compilation of the Unity target interface")
option_end()

option("proxy_dll_name")
    set_default("version.dll")
    set_showmenu(true)
    set_description("Filename the Proxy target builds as, so it loads in place of a real system DLL the game already loads")
option_end()

set_runtimes(is_mode("debug") and "MTd" or "MT")

add_requires("vcpkg::freetype", {configs = {debug = is_mode("debug")}})
add_requires("vcpkg::nlohmann-json", {configs = {debug = is_mode("debug")}})
add_requires("vcpkg::polyhook2", {configs = {debug = is_mode("debug")}})

local frameworkPackages = {"vcpkg::freetype", "vcpkg::nlohmann-json", "vcpkg::polyhook2"}

target("Shared")
    if has_config("avx2") then
        add_vectorexts("avx2")
    end

    set_languages("c++latest")
    set_kind("static")
    set_targetdir(is_mode("debug") and "Build/Debug" or "Build/Release")
    set_pcxxheader("Source/Shared/PCH/pch.h")

    add_includedirs("Source/Shared", "Source/Shared/PCH", { public = true })
    add_includedirs(
        "Source/Shared/Vendor/ImGui",
        "Source/Shared/Vendor/ImGui/misc/cpp",
        "Source/Shared/Vendor/ImGui/misc/freetype",
        { public = true })
    add_defines("IMGUI_ENABLE_FREETYPE", { public = true })

    add_files("Source/Shared/**.cpp")

    add_packages(table.unpack(frameworkPackages), { public = true })

target_end()

target("Payday-Internal-v2")
    if has_config("avx2") then
        add_vectorexts("avx2")
    end

    set_languages("c++latest")
    set_kind("shared")
    set_targetdir(is_mode("debug") and "Build/Debug/Payday-Internal-v2" or "Build/Release/Payday-Internal-v2")
    set_filename("Payday3-Internal-v2-WinGDK.dll")
    set_pcxxheader("Source/Internal/PCH/pch.h")

    add_deps("Shared")
    add_includedirs("Source/Internal", "Source/Internal/PCH")
    add_includedirs("Source/Internal/Vendor/ImGui/backends")

    add_files("Source/Internal/**.cpp")

    if has_config("unreal") then add_files("Source/Internal/Interfaces/Unreal/**.cpp") end
    if has_config("unity") then add_files("Source/Internal/Interfaces/Unity/**.cpp") end

    add_packages(table.unpack(frameworkPackages))
    add_syslinks("d3d11", "d3d12", "dxgi")

target_end()

target("Proxy")
    if has_config("avx2") then
        add_vectorexts("avx2")
    end

    set_languages("c++latest")
    set_kind("shared")
    set_targetdir(is_mode("debug") and "Build/Debug/Proxy" or "Build/Release/Proxy")
    set_filename(get_config("proxy_dll_name"))
    set_pcxxheader("Source/Internal/PCH/pch.h")

    add_deps("Shared")
    add_includedirs("Source/Internal", "Source/Internal/PCH")
    add_includedirs("Source/Internal/Vendor/ImGui/backends")
    add_defines("PROXY")

    add_files("Source/Internal/**.cpp")
    add_files("Source/Proxy/*.cpp")
    add_files("Source/Proxy/Proxy.def")

    if has_config("unreal") then add_files("Source/Internal/Interfaces/Unreal/SDK/**.cpp") end
    if has_config("unity") then add_files("Source/Internal/Interfaces/Unity/SDK/**.cpp") end

    add_packages(table.unpack(frameworkPackages))
    add_syslinks("d3d11", "d3d12", "dxgi")

target_end()
