# Note
> UnknownCheats Thread: https://www.unknowncheats.me/forum/payday-3-a/736601-internal-cheeto.html

# Payday 3 Internal
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/Omega172/Payday3-Internal/total)

Download the latest DLL from [here](https://github.com/Omega172/Payday3-Internal/releases/latest)

For injection I recommend downloading Xenos Injector 2.3.2 from [here](https://www.unknowncheats.me/forum/downloads.php?do=file&id=23686)
or from the official Xenos Github [here](https://github.com/DarthTon/Xenos)

# Contributing
When making any changes they should all happen to the main v2 branch, and then merged into the v2-ms-store branch
to make sure any SDK changes are not merged into the v2-ms-store branch make sure you run `git config merge.ours-sdk.driver true` after checking out the branch for the first time.

## Want to add a translation?
Game strings live in [English.hpp](Source/OmegaWare/Locales/English.hpp) next to the framework's own; add a matching `Locales/<Language>.hpp` entry for each key. Missing keys fall back to English.

## Building

Built on [OmegaWare-Framework](https://github.com/Omega172/OmegaWare-Framework) v4. Requires xmake, Visual Studio with the C++ build tools and vcpkg (for `nlohmann-json`).

```cmd
git clone --recursive https://github.com/Omega172/Payday3-Internal.git
Build.bat release x64
```

In an existing clone run `git submodule update --init --recursive` first. Outputs land in `Build/<Debug|Release>/x64/`:

| Target | Output |
|--------|--------|
| `OmegaWare` | `OmegaWare/Payday3-Internal.dll` for injection |
| `OmegaWareProxy` | `OmegaWareProxy/version.dll`, drop next to the game executable to load without an injector |

`Test.bat debug x64` runs the framework's test suite. After adding or moving files run `UpdateCompileCommands.bat` to refresh IntelliSense.

### Dependencies
- [OmegaWare-GUI](https://github.com/OmegaWareProjects/OmegaWare-GUI), [OmegaWare-RenderHook](https://github.com/OmegaWareProjects/OmegaWare-RenderHook) and [OmegaWare-Localization](https://github.com/OmegaWareProjects/OmegaWare-Localization) as submodules under `Source/Third-Party/OmegaWareProjects/`
- The Dumper-7 SDK in `Source/Third-Party/Dumper-7/CppSDK` (dumped from the Steam build). The framework checks the SDK's global offsets at startup and scans for them when they do not match, so the WinGDK build uses the same SDK.

After dropping in a new Dumper-7 dump, run `FixSdk.bat`. It gives `FOptionalProperty_` placeholders their real sizes (otherwise Dumper-7's own layout asserts fail) and adds the `_parameters.hpp` includes some `_functions.cpp` files are missing. Running it twice is harmless.

## Features
### Player
- [x] Godmode (Set health / Block damage)
- [x] Infinite Stamina
- [x] Instant Melee
- [x] No Screenshake
- [x] No Fall Damage
- [x] No Detection
- [x] Instant Interaction
- [x] Instant Bag Grab
- [x] Instant Minigame
- [x] Custom Display Name
- [x] Auto Secure Thrown Bags (solo / host)
- [x] Pick Up Through Walls (experimental, normal pickup range)
- [x] Client Move (noclip with position sync)
- [ ] Revive

- [x] Host-only teammate options: godmode, infinite ammo, instant interaction, bring to me (No there won't griefing options so don't ask)

- [x] Instant Reload
- [x] Infinite Ammo
- [x] No Recoil
- [x] No Spread
- [x] Custom fire rate

### Visuals
- (Cops / Civilians)
    - [x] Bounding Boxes
    - [x] Names
    - [x] Distance
    - [x] Health Bar
    - [x] Armor Bar
    - [x] Skeleton
    - [x] Highlighting
- Loot (Cash / Medium Money / Large Money / Jewelry)
    - [x] Short names, per-type filters and colors
- Key items (Deposit Boxes / Keycards / QR Phone / USB)
    - [x] Short names, per-type filters and colors
- Keycard readers
    - [x] Short names
- [x] Drop-off zones
- [ ] Bounding boxes for items

### Aimbot
- [x] Snapping
- [x] Silent
- [x] Fov Circle
- [x] Smoothing

### Misc
- [x] Config saving & loading (profiles in `Documents\Payday3-Internal\<exe>\Configs`)

### Possible Future Features
(These are not guaranteed and only ideas that may become features)
- [ ] Lua scripts
- [ ] Player Teleporting (pre-set locations)
- [X] Bag Teleporting
- [ ] Heist Automation
- [ ] Player Hud customisation
- [X] More language support
- [ ] Other packet sniffing and manipulating features

## Usage

### Windows
- **Build**: Follow the build instructions above, or download a release.
- **Injection**: Inject `Payday3-Internal.dll` into `PAYDAY3-Win64-Shipping.exe` (Steam) or `PAYDAY3-WinGDK-Shipping.exe` (Microsoft Store) with any DLL injector, or put the proxy `version.dll` next to the game executable.
- **DLSS / Frame Generation**: the v4 renderer hook is built to survive Streamline, so deleting the Streamline plugin files should no longer be needed.
- **In-Game**: Press `INSERT` to open the menu and `END` to unload. Both keys can be changed under settings > keys. Logs, configs and crash dumps go to `Documents\Payday3-Internal\<exe>\`.

### Proton
**Note:** v2 hasn't been tested on Proton (but most likely does work).

- **Supports Proton:** As of v1.2.13b & [#6](https://github.com/Omega172/Payday3-Internal/pull/6), the cheat should work on both Windows and Linux (proton) versions of the game. Thanks to [alexgot1151](https://github.com/alexgot1151)
#### Set the stage
- Install [protonhax](https://github.com/jcnils/protonhax) on your linux machine.
- Open PAYDAY3's options in Steam and "Force the use of a specific Steam Play compatibility tool" (Use the latest stable release that is available).
- In "General" under Launch options put `protonhax init %COMMAND%  -fileopenlog` the `-fileopenlog` part is if you decide to mod the game.
#### Running and injecting the cheat
- Download the latest release of the precompiled DLL or compile it.
- Put it in the same folder as `injector.exe`, if you are using the [injector](https://github.com/Omega172/Payday3-Internal/releases/tag/Injector) from this repo. If you are using another injector, please follow its instructions on how to use it.
- Open a terminal and navigate to the folder that has the DLL and the `injector.exe` and run the command `protonhax run 1272080 injector.exe`.
