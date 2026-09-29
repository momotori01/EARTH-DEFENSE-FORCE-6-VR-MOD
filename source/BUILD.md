# Building from source

This folder holds the source of every compiled file in the EDF6VR package:

| Folder | Builds | Shipped as |
|---|---|---|
| `_VRDEV/EDF6VR` | the VR mod and its settings program | `Mods/Plugins/EDF6VR.dll`, `EDF6 VR setting.exe` |
| `_VRDEV/EDF6ClearLoot` | item collection on mission clear | `Mods/Plugins/EDF6ClearLoot.dll` |
| `_MultislotDEV` | 8-player online co-op, and the fixed mod loader | `Mods/Plugins/EDF6MultiSlot.dll`, `winmm.dll` |
| `_VRDEV/EDFModLoader-src` | nothing: `PluginAPI.h` from EDFModLoader (BlueAmulet, MIT), used by the three plugins | - |

The folders keep the layout they have next to the game, because the builds read two files of the
installed game: `Root.cpk` (to generate EDF6MultiSlot's menu layout, which is the game's own work, so it
is not in this repository) and `EDF.dll` (for the tests).

## Requirements

- Windows x64.
- Visual Studio 2019 or 2022 (Community or Build Tools) with **Desktop development with C++**.
  Its bundled CMake and Ninja are used; `tools/msvc-x64-env.cmd` finds them through `vswhere`.
- Python 3.10 or newer on `PATH`, with:
  `pip install numpy pillow pefile==2024.8.26 capstone==5.0.9`
- EARTH DEFENSE FORCE 6 (Steam), EDF.dll build 17055427.

## Steps

**0. Put the folders next to the game.** Copy the contents of this `source` folder into the game folder
(`...\steamapps\common\EARTH DEFENSE FORCE 6`), so that `_VRDEV` and `_MultislotDEV` sit beside `EDF6.exe`,
`EDF.dll` and `Root.cpk`. The builds only read the game's files and write only inside these folders.

**1. EDF6VR**

```
cd _VRDEV\EDF6VR
python tools\prepare_native_cockpit_materials.py
build.cmd
```

The first command writes the cockpit material atlases (`assets/cockpit/textures/native_v1`) from the
CC0 textures in `assets/cockpit/textures/cc0` (ambientCG). `build.cmd` configures with
CMake, builds `dist\EDF6VR.dll` and `dist\EDF6 VR setting.exe` (the settings program; its icon is
`assets/settings/vr.ico`, made by `tools/make_settings_icon.py`) and runs the tests. Some tests read the game's `EDF.dll`; the ones that
need the developer's research captures are skipped when those are absent.

**2. EDF6ClearLoot**

```
cd _VRDEV\EDF6ClearLoot
build.cmd
```

Builds `dist\EDF6ClearLoot.dll`.

**3. EDF6MultiSlot and the fixed loader**

```
cd _MultislotDEV
python -B tools\make_menu_label.py
build.cmd
```

Before `build.cmd`, place the **unmodified** EDFModLoader `winmm.dll` at
`_MultislotDEV\third_party\EDFModLoader\winmm.dll`. It is the `winmm.dll` inside `EDF6VR-1.7.3.zip`
on this repository's Releases page, SHA-256
`B80E4DA6AE7264F0E9774C992DE3C5F9B6E9BC9422146ADEEB5BB2BD681E112E`.

`make_menu_label.py` writes `assets\LYT_MAINFRAME.SGO`: the game's menu layout from your `Root.cpk` plus
one empty text field for the "8Player MOD ON/OFF" label. `build.cmd` builds `dist\EDF6MultiSlot.dll`, and
`tools\fix_winmm_proxy.py` turns the loader into `dist\winmm.dll`, SHA-256
`BE94E1FAC0CA12C41B6924E2EB168851641C999CE951D2A5A9FAEA5161B0F9A3`. The fix changes only the 180 export
thunks, 19 bytes each, so that no two threads can be sent into each other's Windows function (see the
docstring in that script). It refuses any other input file.

**4. The package (optional)**

`_VRDEV\EDF6VR\tools\package_release.py` assembles the release ZIP from the three `dist` folders.
The HD texture builder it includes is staged by `tools\stage_hdtexture.py`, which takes texconv,
waifu2x-ncnn-vulkan and the embeddable CPython from `tools\upscale`; `tools\fetch_upscale_tools.py`
downloads those three from their official releases and checks each against a pinned SHA-256.

## Generated sources

Several files under `_VRDEV/EDF6VR/src` are generated data: the cockpit visibility masks
(`*_occlusion.inc`), the cabin shells and the hand rig tables. They were produced by the developer's
research scripts, which are not part of this repository, and are ordinary build inputs here.

## Licenses

Code written for this project is released under the Unlicense (`LICENSE` at the repository root).
These parts keep their own licenses:

- `_VRDEV/EDF6VR/third_party/openxr_headers`: Khronos OpenXR headers, Apache-2.0 OR MIT.
- `_VRDEV/EDFModLoader-src`: EDFModLoader by BlueAmulet, MIT.
- `_VRDEV/EDF6VR/src/cockpit_proteus_stick.inc`: adapted from "Low Poly Hands On Throttle and Stick for VR"
  by marcosgon, CC BY-NC 4.0 (NonCommercial).
- The rest of the list, with the reused CC0 cockpit parts and the game-derived data:
  `_VRDEV/EDF6VR/packaging/THIRD_PARTY_NOTICES.txt`.

EARTH DEFENSE FORCE and all game content belong to their respective owners. No game file is included
in this repository.
