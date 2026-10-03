# EDF6VR 3.1.5

## New

- **Combat frames (Nix, Eiren): less head bob, and the arms got a movable joint. They move much cooler now.**
  - Head bob: in the cockpit, the bounce and rock of each step are smoothed out of your view, and the cockpit and the machine's head stay steady with it. Walking, turning and the body's own turn still come through at once. To change it, set `VehicleBobSmoothSeconds` in EDF6VR.ini (0 = off).
  - Arms: the arms and shoulder weapons point where your right controller points, up to 20°. The arms' speed is in proportion to the machine's turning speed. They stay on when the Nix box in EDF6 VR setting.exe is unticked.

## Fixes

- The headset's own recenter (for example SteamVR's long press) now also resets the eye height and the menu position, as F12 does.
- Power Blade: a swing could move the sheath instead of the blade.
- Combat frame cockpits: the lower screen shows only the radio subtitles, larger than before. The weapon displays no longer show on it.

Still bundles **EDF6MultiSlot 1.5.34**.

To install, use one of these. Your settings are kept.
- Press **Update now** in EDF6 VR setting.exe.
- Run `Update_EDF6VR.bat`.
- Extract the ZIP over your game folder.
