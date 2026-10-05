# EARTH DEFENSE FORCE 6 VR+8Player+Graphic+α MOD

VR mod for the Steam version of EARTH DEFENSE FORCE 6, bundled with an 8-player co-op mod, HD textures and a few quality-of-life extras.

> **Supported: SteamVR and Virtual Desktop.** Meta Quest Link / Air Link may work too, but it has not been tested in detail.
>
> **Meta Quest: seeing double?** Use SteamVR. In SteamVR, open **Settings → OpenXR** and press **Set SteamVR as OpenXR runtime**, then play through SteamVR (Steam Link, or Quest Link with SteamVR).

## Download

Download the latest **EDF6VR ZIP** under **Assets** on the [Releases](https://github.com/momotori01/EARTH-DEFENSE-FORCE-6-VR-MOD/releases) page (not the automatically generated source code archives).

Already installed 2.1.0 or later? Close the game, open **EDF6 VR setting.exe** in the game folder and press **Update now** (or run `Update_EDF6VR.bat`): it updates to the latest release and keeps your settings.

## Features

- Native stereo rendering, room-scale 6DoF tracking.
- **EDF6 VR setting.exe**: one window for everything set outside the game -- update, VR or normal mode, picture size, HD textures, gun hand, a log zip for bug reports, and extra VR settings ([see Setup](#setup)).
- One-handed and two-handed weapon aiming, all four soldier classes and vehicles.
- **Scopes:** when you zoom, the magnified view is shown on the weapon, in its scope or screen, or on a small holographic monitor. The Fencer gets a panel in front of you. The rest of your view stays normal size.
- **Vehicle aim with your gun hand:** point the right controller and the vehicle's aim follows. You can turn it on or off for each kind of vehicle. In combat frames the arms and shoulder weapons also point where you aim, ahead of the body.
- **Left-handed mode** for Rangers, Wing Divers and Air Raiders: hold and fire the gun with the left hand ([how to switch it on](#left-handed-mode)).
- Hand models for all four classes, with fingers that follow the trigger, grip and stick.
- Recoil animation and per-hand firing vibration.
- Rangers can dual wield (reach behind your left shoulder and press grip), each gun with its own reticle.
- Laser sights and thrown-weapon guides leave the weapon in your hand, with
  the landing marker following the line, and they do not lean while you run
  or roll.
- Fencers aim both weapons independently with the game's own heavy aim; hand weapons ride the controllers, shoulder weapons sit on your shoulders. Melee weapons follow the game's own swing while you attack.
- **VR cockpits for every vehicle**, built per vehicle type into the machine's own hull: walkers (Nix, Depth Crawler,
  Barga, Proteus MK2, ...), tanks, combat vehicles, helicopters and trucks. The view stays open to the world, and your
  armour, ammunition and radar are on the cabin's screens. Combat frames show the radio subtitles on a screen of their own and smooth the walking bob out of your view. In the Proteus two-seater you see the other rider, seated, as
  their own soldier.
- **8-player online co-op** (EDF6MultiSlot). It only applies when you host:
  unless you switch it on and make an eight-player room, ordinary online play
  is untouched. While it is on, the room search shows eight-player rooms only.
  You can also match your armour to the lowest player of your
  own class in the room -- the lowest in the room if nobody else is on it --
  so playing alongside a beginner does not leave you far behind in health.
  Other players are also shown where they really are: the base game often
  skipped sending positions for seconds, so people could look metres off.
- **HD textures** (2x), built from your own game files. You can turn them off and on in EDF6 VR setting.exe without deleting them.
- **Lighter Kurul shots:** the Kurul's shotgun pellets are drawn without their swelling ring, so big fights stay smooth.
  It is on by default and can be turned off in EDF6 VR setting.exe.
- Compact HUD: radar, armour and weapons sit together at the corner of your view, or on the inside of a wrist.
- Menus float in the room; messages sit on a panel in front of you, with the radio subtitles near its middle. Other players' nameplates float over their heads and their quick chat bubbles beside them, both seen through walls.
- Automatically collects all remaining items when a mission is cleared (F1).
- Desktop mirror for recording and streaming.

## Installation

1. Extract everything in the ZIP into the game folder, next to `EDF6.exe`
   (for example `\Steam\steamapps\common\EARTH DEFENSE FORCE 6`).
2. VR is on as installed. To play on the monitor instead, open **EDF6 VR setting.exe** (next to `EDF6.exe`), choose **Normal** under **VR mode** and press **Apply**.
3. Start the game from Steam as usual.
4. **The first VR run may be heavy.** The mod measures your headset's field of view on the first run and saves it; until then it renders a wider picture than needed. Close the game once and start it again -- from the second run on, the picture is fitted to your headset and runs lighter. (Do the same once after changing headsets.)

## Updating

Close the game, open **EDF6 VR setting.exe** and press **Update now**. It downloads the newest release, checks it and replaces only the files that changed. Your settings (weapon and hand position, resolution, panel size, ...) are kept, and settings added in the new version are filled in automatically. HD textures stay as they are.

By hand: close the game and extract the new ZIP over your install, replacing files.

**Updating to 2.0 from an older version:** `EDF6VR.ini` is replaced once with the new defaults, since many settings changed. Your resolution is kept and the old file is saved as `EDF6VR.ini.v1.bak`; adjust hand and weapon positions again if needed. If you built HD textures, press **Make** under **HD textures** in EDF6 VR setting.exe once more to repair the textures that went dark at a distance.

If you play in **Normal** mode and extracted an update by hand, choose **Normal** again in EDF6 VR setting.exe.

**From 2.1.5,** `VR_Play.bat`, `Set_Resolution.bat` and `HD_Texture_2x.bat` are replaced by EDF6 VR setting.exe, which deletes them the first time you open it.

## Setup

Everything outside the game is set in **EDF6 VR setting.exe**, next to `EDF6.exe`. Open it with the game closed; each item has its own button, and changes are used the next time you start the game.

> The first time you open it after downloading the ZIP with a web browser, Windows may say "Windows protected your PC". Choose **More info**, then **Run anyway** (the program is not signed).

| Main tab | |
|---|---|
| **Update** | Shows the installed and the newest version. **Update now** installs the newest release; your settings are kept. |
| **VR mode** | **VR** (headset) or **Normal** (monitor, no VR). |
| **Picture size** | **Low 0.8** / **Normal 1.0** / **High 1.25** / **Custom** (0.5 to 2.0). Bigger is sharper but slower. The mod ignores the resolution set in SteamVR or other runtimes. |
| **Scope zoom** | **Native**: sharp, but costs frame rate while zoomed. **Digital**: no frame-rate cost, but blurrier. **Off**: the old whole-view zoom. |
| **HD textures** | **Make** or **Delete** the 2x textures, with progress. Untick **Use HD textures** to turn them off without deleting them. |
| **Crew figures** | **Make** or **Delete** the Proteus two-seater's other rider (about 2 minutes). Making HD textures makes them too. |
| **Gun hand** | **Right** or **Left** (Ranger, Wing Diver, Air Raider). |
| **Problem report** | **Make zip** puts the logs and settings into one zip, saved in the game folder next to EDF6 VR setting.exe (`EDF6VR-logs-<date>-<time>.zip`). Attach it to a bug report. |

| Extra VR settings tab | |
|---|---|
| **VR cockpit** | A cockpit around you in vehicles, on or off. |
| **Vehicle hand aim** | Aim vehicles by pointing your gun hand, on or off for each kind of vehicle (on at first in the Nix only). |
| **Compact HUD** | On or off, and where: the corner of your view, or the right or left wrist. |
| **Aim mark size** | The reticle's size (0.5 is normal). |
| **Sight line thickness** | The thickness of laser sights, throw guides and vehicle aim lines, each on its own (1 is the game's own width; defaults 0.1, 0.2 and 0.3). |
| **Recoil** / **Shot vibration** | The gun kicking back when you shoot; how strongly the controller shakes (0 = off, 1 = strong). |
| **Desktop mirror** | Also shows the game on your monitor, for recording and streaming. |
| **Lighter Kurul shots** | The Kurul's shotgun pellets without their swelling ring. On by default. It is made from your game files when the window opens. |
| **Reset settings** | Every VR setting back to how it came. Your picture size and gun hand stay; the old file is kept as a backup. |

Everything it changes can also be set by hand in `Mods/Plugins/EDF6VR.ini`.

### HD textures
**Make** reads your own game files and writes sharper copies into the `Mods` folder; the game files themselves are not changed.
- Takes about an hour and about 40 GB of free space. Closing the window stops it; **Make** goes on from where it stopped.
- Uses about 300 MB more video memory in game.
- To turn them off, press **Delete**.

### Left-handed mode
Rangers, Wing Divers and Air Raiders can hold and fire the gun with the **left** hand: choose **Left** under **Gun hand** and press **Apply**.

The trigger, grip, buttons and sticks all change sides: you fire with the left trigger, move with the right stick and turn with the left. The gestures below use the other hand, and a Ranger draws the second gun from behind the right shoulder. Fencers, vehicles and menus stay right-handed. By hand, it is at the very top of `Mods/Plugins/EDF6VR.ini`: `LeftHanded=1` (left) or `0` (right, the default); `LeftHandedSticks=0` keeps the sticks where they are.

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
| **F2** / left stick click | 8-player rooms on/off (menu screen, outside a room); while on, the room search lists 8-player rooms only |
| **F3 / Tab** / right stick click | Show squad members 5–8 (in a room) |
| **F4** / left stick click | Match your armour to the room, on/off (in a room) |
| **F11** | VR on/off for this session |
| **F12** | Recenter (also brings the menu back in front of you) |

### Compact HUD
Radar, armour and weapon boxes sit together, smaller, at the bottom-right of your view. Hold the left controller beside your head with the stick **DOWN** for 3 s to move them: view corner → inside of the right wrist → inside of the left wrist. Subtitles and messages stay in front of you. Turning **Compact HUD** off in EDF6 VR setting.exe (Extra VR settings) restores the full-size HUD.

### 8-player co-op
Press **F2**, or click the left stick, on the menu screen **outside a room** to switch 8-player rooms on or off (shown at the bottom left). A room keeps the setting it was made with.
- Hosting with it **ON**, only players who have the 8P mod can join your room; with it **OFF** you make an ordinary room and play with anyone.
- **It also filters the room search.** With it **ON**, only 8-player rooms are listed; with it **OFF**, ordinary and 8-player rooms are both listed and you can join either. To look for an ordinary 4-player room, switch it OFF. Switching while the room list is open searches again straight away.
- Up to 4 players nothing changes. From the 5th on there are more enemies (x1.2, x1.4, x1.6, x1.8 for 5 to 8 players).
- Everyone in an 8-player room -- host and guests -- needs EDF6MultiSlot 1.2.6 or later; the bundled 1.5.33 is recommended for all.

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
- **Supported: SteamVR and Virtual Desktop (VDXR).** Meta Quest Link / Air Link (Meta's own runtime) may work too from 2.1.7, but it has not been tested in detail; if it shows double, switch to SteamVR as above.

Tested with Bigscreen Beyond 2 + Index controllers, PSVR2 (SteamVR), and Quest via Virtual Desktop.

## Known Issues

- Fencer movement audio may occasionally crackle while moving.
- After a revive, a hand model could stretch back toward the body. A fix is in,
  but it has not been seen in a real revive yet.
- Fencer: the aim line of the left shoulder weapon can swing during a dash and
  is slightly off at the moment of a jump.

## Source code

The source of every compiled file in the package (`EDF6VR.dll`, `EDF6MultiSlot.dll`, `EDF6ClearLoot.dll` and the fixed `winmm.dll`) is in [source/](source/), with the build steps in [source/BUILD.md](source/BUILD.md).

Code written for this project is released under the Unlicense: modify it, reuse it and share it, no credit needed. Third-party parts keep their own licenses. Among them, the Proteus control stick is adapted from "Low Poly Hands On Throttle and Stick for VR" by marcosgon (CC BY-NC 4.0, non-commercial). The full list is in [source/BUILD.md](source/BUILD.md#licenses).

## Notes

This mod is provided as-is. No support or continued maintenance is guaranteed.

Not every weapon, equipment item, or vehicle variant has been tested. Some may behave incorrectly.

This mod was made using AI. I cannot read code myself.

If you feel like fixing any bugs, feel free to share your version wherever you like. I'd be happy to give it a try if I come across it.

This is an unofficial fan-made mod. The original game is required, and no original game files are included.
