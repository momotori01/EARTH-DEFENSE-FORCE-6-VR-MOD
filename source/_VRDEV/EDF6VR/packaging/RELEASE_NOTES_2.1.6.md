# EDF6VR 2.1.6

- **New: online, other players are where they really are.** In the base game, other players could look metres away from where they really were, or face the wrong way: the game only put a player's position and facing into some of the updates it sends, and at 60 fps those were often skipped for seconds. EDF6MultiSlot 1.5.32 now sends them in every update (tested with two players: position error 1.6 m → 0.3 m, facing error 117° → 0°, also in flight and in vehicles). You can still play together with people who do not have the mod.
- **EDF6 VR setting.exe.** Everything you set outside the game is in one window, next to EDF6.exe: update, VR or normal mode, picture size, HD textures, gun hand, a log zip for bug reports, and extra VR settings (cockpit, compact HUD, aim mark size, recoil, shot vibration, desktop mirror, reset). Each item has a short note in English and Japanese. It replaces VR_Play.bat, Set_Resolution.bat and HD_Texture_2x.bat, and deletes them the first time you open it.
  If Windows says "Windows protected your PC" the first time you open it, choose **More info**, then **Run anyway** (the program is not signed).
- **Left-handed mode.** Rangers, Wing Divers and Air Raiders can hold and fire the gun with the left hand. The trigger, grip, buttons and sticks all change sides with it (move with the right stick, turn with the left). Fencers and vehicles stay right-handed. To switch it on, open EDF6 VR setting.exe, choose **Left** under **Gun hand** and press **Apply**.
- **Auto-update:** press **Update now** in EDF6 VR setting.exe (or run `Update_EDF6VR.bat`). It updates EDF6VR (with the bundled EDF6MultiSlot) to the latest release here. If you run into a bug, try this first. When a newer version is out, the lower-left line of the menu says so.
- **Cockpits:** better-looking surfaces, with new materials and textures.
- **Fencer:** better controls, closer to the game on a monitor.
- **Fencer:** after a revive, your equipment no longer points the wrong way.
- **Online, known issue (being fixed):** rarely, with certain players in the room, the host cannot press OK to start a mission. If it happens, restart the game.

Bundles EDF6MultiSlot 1.5.32. To install this version, press **Update now** in EDF6 VR setting.exe, run `Update_EDF6VR.bat` (from 2.1.0 on), or extract the ZIP over your game folder. Your settings are kept.
