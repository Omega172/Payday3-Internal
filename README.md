# Payday 3 Internal

An cheat for Payday 3 targeting the steam build.

## About This Project

This project deliberately avoids using my framework in favor of experimenting with:
- **C++20/23 Modules** - Using `.ixx` module interface files instead of traditional headers
- **vcpkg + xmake Integration** - Testing package management with vcpkg in xmake

The goal is experimentation rather than expanding the framework's feature set and examples.

## Building

Requires xmake, vcpkg, and an installation of VisualStudio with the C++ build tools for the compiler.
add the variable VCPKG_ROOT pointing to your vcpkg rood dir to your environment variables

```cmd
xmake config -m debug   # or -m release
xmake build Payday3-Internal
```

Output: `Build/Debug/` or `Build/Release/`

**Note:** If IntelliSense breaks in VS Code, run `update_compile_commands.bat` to rebuild compile commands and potentially fix IntelliSense.

### Dependencies (via vcpkg)
- minhook 1.3.4
- imgui (with win32-binding, dx12-binding)

## Architecture

- **SDK**: Auto-generated Unreal Engine SDK made using [Dumper-7](https://github.com/Encryqed/Dumper-7)
- **Build System**: xmake (C++latest)
- **Module System**: Single-file `.ixx` modules with no separate implementation files

Target game version: `902193`

## Usage

- **Build**: Follow the build instructions above to compile the project.
- **Remove Streamline DLSS**: I have yet to find a fix so stop streamline from crashing the game upon init of the dx12 hook so delete all the files in `C:\Path\To\PAYDAY3\Engine\Plugins\Runtime\Nvidia\Streamline\Binaries\ThirdParty\Win64`
- **Injection**: Use any DLL injector to inject `Payday3-Internal.dll` into the `PAYDAY3Client-Win64-Shipping.exe` process.
- **In-Game**: Press `INSERT` to open the menu and `END` to unload the cheat.
