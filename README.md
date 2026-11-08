# Note
> UnknownCheats Thread: https://www.unknowncheats.me/forum/payday-3-a/736601-internal-cheeto.html

# Payday 3 Internal
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/Omega172/Payday3-Internal/total)

Download the latest DLL from [here](https://github.com/Omega172/Payday3-Internal/releases/latest)

For injection I recommend downloading Xenos Injector 2.3.2 from [here](https://www.unknowncheats.me/forum/downloads.php?do=file&id=23686)
or from the official Xenos Github [here](https://github.com/DarthTon/Xenos)

# Contributing

## Want to add a translation?
Game strings live in [English.hpp](Source/OmegaWare/Locales/English.hpp) next to the framework's own. Missing keys fall back to English, so a partial translation works. Follow these steps for a new language:

1. Copy `Source/OmegaWare/Locales/English.hpp` to `Source/OmegaWare/Locales/<Language>.hpp` and translate the second string of every entry. Do not change the keys. Keep every `{}` placeholder and any game tag in the translated text. The file must be saved as UTF-8.
2. Rename the variable to `g_stLocale<Language>` and set the header fields:
   - `.strCode` is the language code (`de`, `pl`, `ru`, `zh-CN`). If it matches the Windows display language the menu picks it automatically on first launch.
   - `.strName` is the language's own name as shown in the language selector.
   - `.aGlyphRanges` lists extra Unicode ranges the font needs. Latin, Greek and Cyrillic come with the default font. Polish uses `{ { 0x0100, 0x017F } }` for Latin Extended-A. Leave it out if the language only needs the default ranges.
3. Add `#include "<Language>.hpp"` to [Locales.hpp](Source/OmegaWare/Locales/Locales.hpp).
4. Add `g_stLocale<Language>` to the list passed to `OmegaLocalization::GUI::Attach` in [Overlay.cpp](Source/OmegaWare/Overlay/Overlay.cpp). Without this step the language never shows up in the selector.
5. If the script is not covered by the default Windows monospace font (Chinese, Japanese, Korean, Arabic and so on), also add an entry to `ScriptFonts()` in [Overlay.cpp](Source/OmegaWare/Overlay/Overlay.cpp) with the locale and a list of font files from `C:\Windows\Fonts` to try in order (Simplified Chinese uses `msyh.ttc` then `simsun.ttc`). The font is merged into the atlas as a fallback that only supplies characters the main font lacks, so the Latin text keeps its look.
   - Do not list the whole script in `.aGlyphRanges`. The CJK block alone is about 21,000 glyphs and does not fit in the font atlas. For languages listed in `ScriptFonts()` the glyphs are generated from the characters your strings actually use and are always loaded, so switching language never waits for a font rebuild and never shows empty boxes. You never have to maintain ranges by hand.
   - Check that the font has every character you used. A missing character shows as an empty box.
6. Build, start the game, open the menu and pick the language. Check the log in `Documents\Payday3-Internal\<exe>\Logs` for a "no installed font covers ..." warning if text shows as boxes.

Players can also add or override strings without rebuilding by putting locale files in the `Locales` folder next to the logs. The format is described in the [OmegaWare-Localization readme](https://github.com/OmegaWareProjects/OmegaWare-Localization/blob/main/README.md). Characters that are not in the built-in strings of a `ScriptFonts()` language will not have glyphs baked.

## Profiling

Two tools show whether the mod is behind a slowdown or a leak:

- **Profiler page** (menu, misc, profiler): times every feature stage, every game thread task and every hooked game function, and lists the most expensive ones per second. With "write slow spots to the log" on, anything over 1 ms per second or 12 ms in one call is logged every 5 seconds, and the log gets the memory, handle and thread counts once a minute so a leak shows as numbers that keep climbing.
- **`Scripts/ProfileGame.bat`** (or `.ps1` and `.sh`): run it from outside the game, it waits for `PAYDAY3-Win64-Shipping`, samples CPU, memory, handles and threads every second into `Build/Profile/*.csv` and attributes CPU to threads started by the mod's DLLs. Close the game or press Ctrl+C for a summary and a verdict. Run it once with the mod and once without to compare. Work the mod does inside game threads is not visible to it, use the profiler page and log for that.

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
- The Dumper-7 SDK in `Source/Third-Party/Dumper-7/CppSDK`: dumped from the Steam build. It works for the Microsoft Store (WinGDK) build too, because the SDK audit corrects the offsets at runtime. The framework checks the SDK's global offsets at startup and scans for them when they do not match, so a slightly stale dump still finds the engine globals.

CI and releases run `Scripts/BuildPackage.ps1`, which builds, runs the tests and packages the DLL and the `version.dll` proxy. Locally: `powershell -File Scripts/BuildPackage.ps1 -Modes release`.

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
Every option is set separately for each category.
- Enemies (cops, plus security cameras as an option) / Civilians
    - [x] Bounding Boxes
    - [x] Names
    - [x] Distance
    - [x] Health Bar
    - [x] Armor Bar (enemies)
    - [x] Skeleton
    - [x] Outline through walls
- Loot (Cash / Medium Money / Large Money / Jewelry / ATMs that have not been looted)
    - [x] Short names, distance, outline, per-type filters and colors
- Key items (Deposit Boxes / Keycards / QR Phone / USB)
    - [x] Short names, distance, outline, per-type filters and colors
- Objectives (Keycard readers / Drop-off zones)
    - [x] Short names, distance, outline (readers), filters and colors
- [x] Recolor the game's own outlines
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
- **Injection**: Inject `Payday3-Internal.dll` into `PAYDAY3-Win64-Shipping.exe` (Steam) or `PAYDAY3-WinGDK-Shipping.exe` (Microsoft Store) with any DLL injector, or put `Proxy/version.dll` next to the game executable.
- **DLSS / Frame Generation**: the v4 renderer hook is built to survive Streamline, so deleting the Streamline plugin files should no longer be needed.
- **In-Game**: Press `INSERT` to open the menu and `END` to unload. Both keys can be changed under settings > keys. Logs, configs and crash dumps go to `Documents\Payday3-Internal\<exe>\`.

### Proton
**Note:** v3 hasn't been tested on Proton (but most likely does work).

- **Supports Proton:** As of v1.2.13b & [#6](https://github.com/Omega172/Payday3-Internal/pull/6), the cheat should work on both Windows and Linux (proton) versions of the game. Thanks to [alexgot1151](https://github.com/alexgot1151)
#### Set the stage
- Install [protonhax](https://github.com/jcnils/protonhax) on your linux machine.
- Open PAYDAY3's options in Steam and "Force the use of a specific Steam Play compatibility tool" (Use the latest stable release that is available).
- In "General" under Launch options put `protonhax init %COMMAND%  -fileopenlog` the `-fileopenlog` part is if you decide to mod the game.
#### Running and injecting the cheat
- Download the latest release of the precompiled DLL or compile it.
- Put it in the same folder as `injector.exe`, if you are using the [injector](https://github.com/Omega172/Payday3-Internal/releases/tag/Injector) from this repo. If you are using another injector, please follow its instructions on how to use it.
- Open a terminal and navigate to the folder that has the DLL and the `injector.exe` and run the command `protonhax run 1272080 injector.exe`.
