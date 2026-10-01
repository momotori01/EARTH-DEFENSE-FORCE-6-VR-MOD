# EDF6VR 3.0.2

## New

- **Scopes.** When you zoom, the magnified view is shown on the weapon and the rest of your view stays normal size.
  - Scoped weapons: inside the scope, round or rectangular to match it.
  - Air Raider marker guns: on the gun's own screen.
  - Zoom weapons without a scope: on a small blue holographic monitor above the sights.
  - Fencer: on a green panel in front of you, looking where the zooming hand aims.

  Choose how it is drawn in EDF6 VR setting.exe, under **Scope zoom**:
  - **Native**: sharp, but costs frame rate while zoomed.
  - **Digital**: no frame-rate cost, but blurrier.
  - **Off**: the old whole-view zoom.
- **Vehicles: aim with your gun hand.** Point the right controller and the vehicle's aim follows, as if you pushed the right stick. The stick still works. You can turn this on or off for each kind of vehicle under **Extra VR settings → Vehicle hand aim**. The Depth and the Barga start off.
- **Proteus two-seater: you see the other rider.** They sit in the cockpit as their own soldier, with their own look and colours. The figures are made from your game files: press **Crew figures → Make** in EDF6 VR setting.exe (about 2 minutes), or they are made along with the HD textures.
- **Lighter Kurul shots.** The Kurul's shotgun pellets are drawn without the ring that swells around each one, so big Kurul fights stay smooth.
  - It is on by default and is made from your game files when you open EDF6 VR setting.exe or press **Make**.
  - To turn it off: **Extra VR settings → Lighter Kurul shots**.
- **Sight line thickness.** Laser sights, throw guides (grenades, turrets) and vehicle aim lines are drawn thinner: 0.1, 0.2 and 0.3 of the game's own width. You can set each one under **Extra VR settings → Sight line thickness**; 1 is the game's own width.

## Fixes

- **White ground with HD textures** on 23 maps, including mission 65. If you made HD textures, press **Make** under **HD textures** once. Only the 24 broken files are made again.
- **Fencer shield:**
  - A shield in the left hand now guards where the left hand points. Before, it guarded where the right hand pointed.
  - A raised shield always comes up square in front of its controller.
- **The laser sight seen in a scope** no longer slides away while you walk or roll.
- **Scope crosshair** now tilts with the gun, not with your head.
- **Scope position** fixed on several weapons.

## Small fixes

- The Brute's door gun no longer has up and down swapped.
- Proteus two-seater:
  - The information monitor no longer disappears.
  - The base of the rear lever no longer flickers.
- The cockpit hand levers (Nix, Nix chest, Barga, Depth, Proteus two-seater) sit further forward and are slimmer.
- EDF6 VR setting.exe scrolls when it is taller than your screen.

Bundles **EDF6MultiSlot 1.5.33**. The only change is extra logging to find why 8-player missions sometimes drop everyone.

To install, use one of these. Your settings are kept.
- Press **Update now** in EDF6 VR setting.exe.
- Run `Update_EDF6VR.bat`.
- Extract the ZIP over your game folder.
