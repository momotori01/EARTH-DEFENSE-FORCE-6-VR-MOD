# EARTH DEFENSE FORCE 6 VR+8Player+Graphic+α MOD

VR mod for the Steam version of EARTH DEFENSE FORCE 6, bundled with an 8-player co-op mod, HD textures and a few quality-of-life extras.

## Download

Download the latest **EDF6VR ZIP** under **Assets** on the [Releases](https://github.com/momotori01/EARTH-DEFENSE-FORCE-6-VR-MOD/releases) page (not the automatically generated source code archives).

## Features

- Native stereo rendering, room-scale 6DoF tracking.
- One-handed and two-handed weapon aiming, all four soldier classes and vehicles.
- Hand models for all four classes, with fingers that follow the trigger, grip and stick.
- Recoil animation and per-hand firing vibration.
- Rangers can dual wield (reach behind your left shoulder and press grip).
- Laser sights and thrown-weapon guides leave the weapon in your hand, with
  the landing marker following the line, and they do not lean while you run
  or roll.
- Fencers aim both weapons independently with the game's own heavy aim; hand weapons ride the controllers, shoulder weapons sit on your shoulders.
- **8-player online co-op** (EDF6MultiSlot). It only applies when you host:
  unless you switch it on and make an eight-player room, ordinary online play
  is untouched. You can also match your armour to the lowest player of your
  own class in the room -- the lowest in the room if nobody else is on it --
  so playing alongside a beginner does not leave you far behind in health.
- **HD textures** (2x), built from your own game files.
- Compact HUD: radar, armour and weapons sit together at the corner of your view, or on the inside of a wrist.
- Menus float in the room; subtitles and messages sit on a panel in front of you. Other players' nameplates float over their heads and show through walls.
- Automatically collects all remaining items when a mission is cleared (F1).
- Desktop mirror for recording and streaming.

## Installation

1. Extract everything in the ZIP into the game folder, next to `EDF6.exe`
   (for example `\Steam\steamapps\common\EARTH DEFENSE FORCE 6`).
2. Run `VR_Play.bat`: **Y** = VR, **N** = normal flat play. Close the game before switching.
3. Start the game from Steam as usual.
4. **The first VR run may be heavy.** The mod measures your headset's field of view on the first run and saves it; until then it renders a wider picture than needed. Close the game once and start it again -- from the second run on, the picture is fitted to your headset and runs lighter. (Do the same once after changing headsets.)

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
| **Move the compact HUD** | Left controller beside your head, stick **DOWN** for 3 s: view corner → right wrist → left wrist |
| **Ranger dual wield** | Left hand behind your left shoulder / back, press grip |

In adjust mode: left stick moves, triggers raise/lower, right stick turns, grips roll. **Right A** saves, **Left A/X** cancels, clicking **both sticks** resets. Hand position is saved per controller type (Index, Quest, ...).

### Keys
| Key | |
|---|---|
| **F1** | Collect all items on mission clear, on/off |
| **F2** / left stick click | 8-player rooms on/off (menu screen, outside a room) |
| **F3 / Tab** / right stick click | Show squad members 5–8 (in a room) |
| **F4** / left stick click | Match your armour to the room, on/off (in a room) |
| **F11** | VR on/off for this session |
| **F12** | Recenter (also brings the menu back in front of you) |

### Compact HUD
Radar, armour and weapon boxes sit together, smaller, at the bottom-right of your view. Hold the left controller beside your head with the stick **DOWN** for 3 s to move them: view corner → inside of the right wrist → inside of the left wrist. Subtitles and messages stay in front of you. `[Render] UiCluster=0` in `EDF6VR.ini` restores the full-size HUD.

### 8-player co-op
Press **F2**, or click the left stick, on the menu screen **outside a room** to switch 8-player rooms on or off (shown at the bottom left). A room keeps the setting it was made with.
- It only applies when you host. Hosting with it **ON**, only players who have the 8P mod can join your room; with it **OFF** you make an ordinary room and play with anyone.
- As a guest the setting doesn't matter: you can find and join both kinds of room.
- Up to 4 players nothing changes. From the 5th on there are more enemies (x1.2, x1.4, x1.6, x1.8 for 5 to 8 players), and their durability, damage and speed stay exactly as they are with four.
- Everyone in an 8-player room -- host and guests -- needs EDF6MultiSlot 1.2.6 or later.

### Matching armour
In a room, press **F4** or click the left stick to turn armour matching on or off (shown at the bottom left). It is for playing alongside a beginner, or on a class you rarely use, without being far behind in health.
- Your armour is raised to the lowest of the others in the room: the lowest of your own class if anyone else is on it, otherwise the lowest in the room. It only ever raises, never lowers.
- Anyone within 300 of you is skipped and the next one up is taken, so two beginners cannot hold each other down. In a room of 3000 / 2000 / 300 / 200, both the 300 and the 200 copy 2000.
- Each class has a ceiling: Ranger 4000, Wing Diver 2500, Air Raider 3000, Fencer 4000. At the ceiling the display reads `copy armor :2500(Max)`.
- Nothing is written to your save. Turning it off, or leaving the room, puts it back. The member list always shows everyone's real armour -- the raise applies inside the mission -- so check the number at the bottom left to see it working.
- The 300 and the ceilings can be changed under `[CopyArmor]` in `EDF6MultiSlot.ini`.
- **F4** is also this mod's frame-dump key, but only while `[Diagnostics] DevKeys=1` in `EDF6VR.ini`, which ships off. If you turn that on, change one of them.

More settings are described in `Mods/Plugins/EDF6VR.ini` (created on first start; the defaults are in `EDF6VR/EDF6VR.defaults.ini`), `README_EDF6VR.txt` and `README_EDF6MultiSlot.txt`.

## Requirements

- EARTH DEFENSE FORCE 6 (Steam).
- An OpenXR-compatible VR headset with motion controllers.
- An OpenXR runtime such as SteamVR or VDXR (Virtual Desktop).

Tested with Bigscreen Beyond 2 + Index controllers, PSVR2 (SteamVR), and Quest via Virtual Desktop.

## Known Issues

- Fencer movement audio may occasionally crackle while moving.
- After a revive, a Fencer's hand model can stretch back toward the body for
  the rest of the mission.

## Notes

This mod is provided as-is. No support or continued maintenance is guaranteed.

Not every weapon, equipment item, or vehicle variant has been tested. Some may behave incorrectly.

This mod was made using AI. I cannot read code myself.

If you feel like fixing any bugs, feel free to share your version wherever you like. I'd be happy to give it a try if I come across it.

This is an unofficial fan-made mod. The original game is required, and no original game files are included.
