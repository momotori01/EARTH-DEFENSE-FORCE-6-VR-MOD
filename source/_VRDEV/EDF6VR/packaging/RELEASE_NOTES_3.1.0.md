# EDF6VR 3.1.0

## New

- **Fencer melee weapons swing as in the game.** Melee weapons other than spears are held in your hand, as spears are. When you attack, the weapon follows the game's own swing, your arm's travel included, and comes back to your hand when the swing ends. Charging does not move it, and the other hand is not affected. Spears are unchanged.
- **Radio subtitles near the middle.** The radio subtitles the game draws at the bottom of the screen are moved up toward the middle of the HUD panel, so they are easier to read and stay clear of the weapon displays. The compact HUD no longer takes pieces of them. When a subtitle runs to four lines, the ends of the fourth line can be cut slightly. To keep them at the bottom, set `SubtitleCentre=0` in EDF6VR.ini.
- **Quick chat bubbles beside each player.** A player's quick chat bubble now appears beside that player in the world, like the nameplates, instead of on the HUD panel. In VR the bubble and its text are drawn smaller, a little larger than the nameplates. With VR off they keep the game's size. To change the size, set `WorldChatBubbleSize` in EDF6VR.ini (1.0 = the game's own).

## Fixes

- **Fencer compact HUD:** the charge gauges and the reload gauges were cut off. The Fencer's compact HUD is now laid out from the game's own HUD data: radar and armour on top, the two weapons below, each charge gauge at its outer end, and the reload gauges included. A stray reload mark left on the HUD panel is gone.

## Small fixes

- Fencer shield: a lowered shield now stays fixed to the controller at your side, as a raised one does in front.
- Power Blade: the sheath stays on your shoulder instead of swinging with the blade.
- Quick chat window: while it is open, the HUD is shown normally, without the compact HUD, so the window is no longer cut off.

Bundles **EDF6MultiSlot 1.5.34**. The only change is corrected logging for finding why 8-player missions sometimes drop everyone.

To install, use one of these. Your settings are kept.
- Press **Update now** in EDF6 VR setting.exe.
- Run `Update_EDF6VR.bat`.
- Extract the ZIP over your game folder.
