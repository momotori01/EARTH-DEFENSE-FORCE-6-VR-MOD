EDF6 Clear Loot 0.1.2
====================

This is a SEPARATE EDFModLoader plugin. It works without EDF6VR or a headset.
It collects remaining weapon, armor and health drops through the game's normal
pickup routine on the next normal soldier update after detecting the mission-clear
UI or jingle, collecting around the appearance of "Mission Cleared".
It does not invoke pickup from the result transition.
Normal in-mission pickup distance, drops, reward limits and save logic are unchanged.
Aborting or retrying a mission does not trigger collection.

Files: Mods/Plugins/EDF6ClearLoot.dll and EDF6ClearLoot.ini.
Default: Enabled=1 in [ClearLoot].

While in a mission with the game focused, press F1 to toggle ON/OFF.
Turning ON requests the native item pickup sound once. No item or
health is awarded by this notification. OFF is silent. Holding F1 does not repeat.
The setting is saved immediately to EDF6ClearLoot.ini and persists after restart.
F1 is not processed at the title screen or in menus without soldier updates.
EDF6VR's previous F1 anti-aliasing shortcut has been removed to avoid conflicts.

VR_Play.bat ONLY enables/disables EDF6VR. Clear Loot retains its own setting even
when playing without VR. To remove it, close the game and remove EDF6ClearLoot.dll.

In-game verified: F1 toggle and confirmation sound, remaining drops collected near
"Mission Cleared", and normal transition to results. Online and split-screen are unverified.
If the clear signal or a recently validated pickup recipient is unavailable, this plugin
skips collection and lets the game's result processing continue.

Log: Mods/Plugins/EDF6ClearLoot.log (EXIT and COLLECT lines).
Compatible game profile: tested EDF.dll build 17055427. Unsupported/conflicting
code profiles are refused. Do not combine with other autoloot patches.
