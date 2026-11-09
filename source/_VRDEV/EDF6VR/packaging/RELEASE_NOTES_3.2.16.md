# EDF6VR 3.2.16

## Changes

- **Ranger, two guns:** pressing reload now puts the left gun away and reloads. Before, reload did nothing while you held two guns.
- **Compact HUD:** it no longer moves when you hold the stick down beside your left temple for 3 seconds; there the stick now just moves you. Choose where the HUD goes (corner, right wrist, left wrist or off) under **VR setting** in the Start menu or in EDF6 VR setting.exe.
- **Video memory:** the game's own screen now uses 4x anti-aliasing instead of 8x. The 3D world was never smoothed this way, only the edges of the HUD, which look the same. This frees about half a gigabyte of video memory. `[Render] GameMsaa=8` in `Mods/Plugins/EDF6VR.ini` brings back the game's 8x.
- The log now records the game's video memory use every 5 seconds, which helps when you report stutter.

Still bundles **EDF6MultiSlot 1.6.12**.

To install, use one of these. Your settings are kept.
- Press **Update now** in EDF6 VR setting.exe.
- Run `Update_EDF6VR.bat`.
- Extract the ZIP over your game folder.
