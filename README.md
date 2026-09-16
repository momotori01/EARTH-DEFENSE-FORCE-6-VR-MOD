# EARTH DEFENSE FORCE 6 VR+8Player+Graphic+α MOD

VR mod for the Steam version of EARTH DEFENSE FORCE 6, bundled with an 8-player co-op mod, HD textures and a few quality-of-life extras.

## Download

Download the latest **EDF6VR ZIP** under **Assets** on the [Releases](https://github.com/momotori01/EARTH-DEFENSE-FORCE-6-VR-MOD/releases) page (not the automatically generated source code archives).

## Features

- Native stereo rendering, room-scale 6DoF tracking.
- One-handed and two-handed weapon aiming, all four soldier classes and vehicles.
- Hand models for Ranger, Wing Diver and Air Raider, with fingers that follow the trigger, grip and stick.
- Recoil animation and per-hand firing vibration.
- Rangers can dual wield (reach behind your left shoulder and press grip).
- **8-player online co-op** (EDF6MultiSlot).
- **HD textures** (2x), built from your own game files.
- Menus float in the room; the HUD sits on a panel in front of you.
- Automatically collects all remaining items when a mission is cleared (F1).
- Desktop mirror for recording and streaming.

## Installation

1. Extract everything in the ZIP into the game folder, next to `EDF6.exe`
   (for example `\Steam\steamapps\common\EARTH DEFENSE FORCE 6`).
2. Run `VR_Play.bat`: **Y** = VR, **N** = normal flat play. Close the game before switching.
3. Start the game from Steam as usual.

## Updating

Close the game and extract the new ZIP over your install, replacing files. Your settings (weapon and hand position, resolution, panel size, ...) are kept, and settings added in the new version are filled in automatically. HD textures stay as they are.

If you had chosen **N** in `VR_Play.bat`, run it again after updating.

## Setup

### Resolution
Run `Set_Resolution.bat` and pick a size: **Low 0.8** / **Normal 1.0** / **High 1.25** / **Custom** (up to 2.0). Bigger is sharper but slower. The mod ignores the resolution set in SteamVR or other runtimes. Restart the game after changing it.

### HD textures
Run `HD_Texture_2x.bat` and answer **y**. It reads your own game files and writes sharper copies into the `Mods` folder; the game files themselves are not changed.
- Takes about an hour and about 40 GB of free space.
- Uses about 300 MB more video memory in game.
- To turn them off, run it again and answer **n**, then **y** to delete the files.

## Controls

### Gestures
| | How |
|---|---|
| **D-pad / Start / Select** | Right controller beside the right side of your head |
| **Recenter view (height)** | Left controller beside the left side of your head, stick **UP** for 3 s |
| **Adjust weapon position** | Left controller beside your head, stick **RIGHT** for 3 s |
| **Adjust hand position** | Left controller beside your head, stick **LEFT** for 3 s |
| **Ranger dual wield** | Left hand behind your left shoulder / back, press grip |

In adjust mode: left stick moves, triggers raise/lower, right stick turns, grips roll. **Right A** saves, **Left A/X** cancels, clicking **both sticks** resets. Hand position is saved per controller type (Index, Quest, ...).

### Keys
| Key | |
|---|---|
| **F1** | Collect all items on mission clear, on/off |
| **F2** | 8-player rooms on/off (room menu) |
| **F3 / Tab** | Show squad members 5–8 |
| **F11** | VR on/off for this session |
| **F12** | Recenter (also brings the menu back in front of you) |

### 8-player co-op
Press **F2** on the room menu to switch 8-player rooms on or off (shown at the bottom left).
- Hosting with it **ON**: only players who have the 8P mod can join your room.
- As a guest the setting doesn't matter.
- Up to 4 players nothing changes. From the 5th player on, the number of enemies grows with each extra player; enemy stats are capped at the 5-player level.

More settings are described in `Mods/Plugins/EDF6VR.ini` (created on first start; the defaults are in `EDF6VR/EDF6VR.defaults.ini`), `README_EDF6VR.txt` and `README_EDF6MultiSlot.txt`.

## Requirements

- EARTH DEFENSE FORCE 6 (Steam).
- An OpenXR-compatible VR headset with motion controllers.
- An OpenXR runtime such as SteamVR or VDXR (Virtual Desktop).

Tested with Bigscreen Beyond 2 + Index controllers, PSVR2 (SteamVR), and Quest via Virtual Desktop.

## Known Issues

- Fencer movement audio may occasionally crackle while moving.

## Notes

This mod is provided as-is. No support or continued maintenance is guaranteed.

Not every weapon, equipment item, or vehicle variant has been tested. Some may behave incorrectly.

This mod was made using AI. I cannot read code myself.

If you feel like fixing any bugs, feel free to share your version wherever you like. I'd be happy to give it a try if I come across it.

This is an unofficial fan-made mod. The original game is required, and no original game files are included.
