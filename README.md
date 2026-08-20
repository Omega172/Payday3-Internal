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

## Building

Requires xmake, and an installation of VisualStudio with the C++ build tools for the compiler.

```cmd
xmake config -m debug   # or -m release
xmake build
```

Output: `Build/Debug/` or `Build/Release/`

**Note:** If IntelliSense breaks in VS Code, run `update_compile_commands.bat` to rebuild compile commands and potentially fix IntelliSense.

### Dependencies (via xmake repo)
- minhook
- imgui (with win32-binding, dx12-binding)

## Features
### Player
- [x] Godmode (Set health / Block damage)
- [x] Infinite Stamina
- [x] Instant Melee
- [x] No Screenshake
- [x] No Fall Damage
- [x] No Detection
- [ ] Client Move
- [ ] Fly
- [ ] Revive

- [ ] Mods for other players in game (No there won't griefing options so don't ask)

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
- (Cash / Deposit Box / Keycards)
    - [x] Names
    - [ ] Bounding Boxes

### Aimbot
- [x] Snapping
- [x] Silent
- [x] Fov Circle
- [x] Smoothing

### Misc
- [ ] Config saving & loading

### Possible Future Features
(These are not guaranteed and only ideas that may become features)
- [ ] Lua scripts
- [ ] Flying
- [ ] Player Teleporting (pre-set locations)
- [ ] Bag Teleporting
- [ ] Heist Automation
- [ ] Player Hud customisation
- [ ] Menu customisation
- [ ] More language support
- [ ] Other packet sniffing and manipulating features

## Usage

### Windows
- **Build**: Follow the build instructions above to compile the project.
- **Remove Streamline DLSS**: I have yet to find a fix so stop streamline from crashing the game upon init of the dx12 hook so delete all the files in `C:\Path\To\PAYDAY3\Engine\Plugins\Runtime\Nvidia\Streamline\Binaries\ThirdParty\Win64`
- **Injection**: Use any DLL injector to inject `Payday3-Internal.dll` into the `PAYDAY3Client-Win64-Shipping.exe` process.
- **In-Game**: Press `INSERT` to open the menu and `END` to unload the cheat.

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
