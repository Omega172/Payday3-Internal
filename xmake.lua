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

add_requires("vcpkg::freetype", {
    configs = { debug = is_mode("debug") }
})

add_requires("vcpkg::nlohmann-json", {
    configs = { debug = is_mode("debug") }
})

add_requires("vcpkg::polyhook2", {
    configs = { debug = is_mode("debug") }
})

add_requires("vcpkg::asmjit", {
    configs = { debug = is_mode("debug") }
})

add_requires("vcpkg::asmtk", {
    configs = { debug = is_mode("debug") }
})

add_requires("vcpkg::zydis", {
    configs = { debug = is_mode("debug") }
})

add_requires("vcpkg::libpng", {
    configs = { debug = is_mode("debug") }
})

add_requires("vcpkg::zlib", {
    configs = { debug = is_mode("debug") }
})

add_requires("vcpkg::bzip2", {
    configs = { debug = is_mode("debug") }
})

add_requires("vcpkg::brotli", {
    configs = { debug = is_mode("debug") }
})

local frameworkPackages =
{
    "vcpkg::freetype",
    "vcpkg::nlohmann-json",
    "vcpkg::polyhook2",

    "vcpkg::asmjit",
    "vcpkg::asmtk",
    "vcpkg::zydis",

    "vcpkg::libpng",
    "vcpkg::zlib",
    "vcpkg::bzip2",
    "vcpkg::brotli"
}

local function add_shared_target(platformName, wingdk)
    local targetName = "Shared-" .. platformName
    local buildRoot = path.join("Build", is_mode("debug") and "Debug" or "Release", platformName)

    target(targetName)
        if has_config("avx2") then
            add_vectorexts("avx2")
        end

        add_defines("GAME_WINGDK=" .. (wingdk and "1" or "0"), { public = true })
        set_languages("c++latest")
        set_kind("static")
        set_targetdir(buildRoot)
        set_pcxxheader("Source/Shared/PCH/pch.h")
        add_includedirs(path.join(os.projectdir(), "Source/Internal/Interfaces/Unreal"), { public = true })
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
end

local function add_internal_target(platformName, wingdk)
    local sharedTarget = "Shared-" .. platformName
    local targetName = "Payday-Internal-v2-" .. platformName
    local buildRoot = path.join("Build", is_mode("debug") and "Debug" or "Release", platformName)

    target(targetName)
        if has_config("avx2") then
            add_vectorexts("avx2")
        end

        add_defines("GAME_WINGDK=" .. (wingdk and "1" or "0"))
        set_languages("c++latest")
        set_kind("shared")
        set_targetdir(path.join(buildRoot, "Payday-Internal-v2"))
        set_pcxxheader("Source/Internal/PCH/pch.h")
        add_deps(sharedTarget)
        add_includedirs("Source/Internal", "Source/Internal/PCH")
        add_includedirs("Source/Internal/Vendor/ImGui/backends")
        add_files("Source/Internal/**.cpp")
        if has_config("unreal") then add_files("Source/Internal/Interfaces/Unreal/SDK/**.cpp") end
        if has_config("unity") then add_files("Source/Internal/Interfaces/Unity/SDK/**.cpp") end
        add_packages(table.unpack(frameworkPackages))
        add_syslinks("d3d11", "d3d12", "dxgi")
    target_end()
end

local function add_proxy_target(platformName, wingdk)
    local sharedTarget = "Shared-" .. platformName
    local targetName = "Proxy-" .. platformName
    local buildRoot = path.join("Build", is_mode("debug") and "Debug" or "Release", platformName)

    target(targetName)
        if has_config("avx2") then
            add_vectorexts("avx2")
        end

        add_defines("GAME_WINGDK=" .. (wingdk and "1" or "0"), "PROXY")
        set_languages("c++latest")
        set_kind("shared")
        set_targetdir(path.join(buildRoot, "Proxy"))
        set_filename(get_config("proxy_dll_name"))
        set_pcxxheader("Source/Internal/PCH/pch.h")
        add_deps(sharedTarget)
        add_includedirs("Source/Internal", "Source/Internal/PCH")
        add_includedirs("Source/Internal/Vendor/ImGui/backends")
        add_files("Source/Internal/**.cpp")
        add_files("Source/Proxy/*.cpp")
        add_files("Source/Proxy/Proxy.def")
        if has_config("unreal") then add_files("Source/Internal/Interfaces/Unreal/SDK/**.cpp") end
        if has_config("unity") then add_files("Source/Internal/Interfaces/Unity/SDK/**.cpp") end
        add_packages(table.unpack(frameworkPackages))
        add_syslinks("d3d11", "d3d12", "dxgi")
    target_end()
end

add_shared_target("Steam", false)
add_internal_target("Steam", false)
add_proxy_target("Steam", false)
add_shared_target("WinGDK", true)
add_internal_target("WinGDK", true)
add_proxy_target("WinGDK", true)
