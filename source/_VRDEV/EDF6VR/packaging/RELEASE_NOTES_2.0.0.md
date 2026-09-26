# EDF6VR 2.0.0

**VR cockpits for every vehicle**

Every vehicle now has a seated VR cockpit of its own, built for its type into the machine's real hull.  Each cabin shows your armour, ammunition and radar on its own screens.


**Eight-player co-op (EDF6MultiSlot 1.5.6)**

- With 8-player rooms switched **ON**, the room search lists 8-player rooms only. **OFF** lists ordinary and 8-player rooms as before. Switching (F2 / left stick click) while the room list is open searches again straight away.
- Several players dropping out of a room at once: when the last reply of the join check fails to send, it is now sent once more.
- The bundled mod loader (`winmm.dll`) no longer lets two threads send each other into the wrong Windows function.
- Everyone in an 8-player room should use this version.

**Fixes since 1.6**

- Aim: no dead band around the hand; two-handed aim follows the line of the palms, and one-handed points the same way.
- Turning with the stick no longer shakes the held weapon and hands.
- Hands come back after changing class, and the open wrist of the hand models is closed.
- First person no longer drops back to third person after eight or nine missions.
- Vehicles: the view is level with the horizon instead of pitched into the ground.
- Compact HUD: the reload gauge under the weapon boxes is no longer cut off.
- Ranger: laser sights and thrown-weapon guides start at the weapon in your hand and no longer lean while you run or roll. In dual wield, a left-hand shot during the right-hand reload no longer goes off to one side.
- Wing Diver: aiming straight up no longer sends you off.
- Air Raider: an ant carrying you off no longer throws the view onto the flat panel. The radio no longer turns black when held away from your face with HD textures.
- Fencer: hand weapons aim along the barrel, the muzzle flash comes out of the muzzle, and left-hand shots land on the reticle with each weapon's spread kept.
- Fencer: shoulder weapons stay steady through dashes and jumps.
- Fencer: the shield no longer flips a quarter turn; it is lowered at rest, comes up to guard while you hold its trigger, and stays steady through dashes and jumps.
- Fencer: weapons come straight back after an ant shakes you, and a hand weapon no longer points off in a wrong direction now and then.

**Updating from an older version**

- Extract the whole archive: `winmm.dll` changes too.
- `EDF6VR.ini` is replaced once with the new defaults, since many settings changed. Your resolution (Set_Resolution.bat) is kept, and the old file is saved as `EDF6VR.ini.v1.bak`. Hand and weapon positions go back to the defaults; adjust them again if needed.
- If you built HD textures, run `HD_Texture_2x.bat` once more. It repairs only the textures that went dark at a distance.

**Known**

- Fencer: the aim line of the left shoulder weapon can swing during a dash and is slightly off at the moment of a jump.
- After a revive a hand model could stretch back toward the body. A fix is in, but it has not been seen in a real revive yet.
