# EDF6VR 3.2.17

## Fixes

- **Eight players online:** starting a mission no longer drops every guest. With eight players the host's start message (everyone's equipment) can be bigger than one network packet may be (1170 bytes). It was refused, and about 27 seconds later all guests were disconnected while the host started alone. **EDF6MultiSlot 1.6.13** sends such a packet in parts and puts it back together. Everyone in an eight-player room needs 1.6.13.
- Quitting the game no longer ends in a crash report. EDF6VR left a background task running, and Windows recorded a crash as the game closed.

Now bundles **EDF6MultiSlot 1.6.13** (plays together with 1.6.12).

To install, use one of these. Your settings are kept.
- Press **Update now** in EDF6 VR setting.exe.
- Run `Update_EDF6VR.bat`.
- Extract the ZIP over your game folder.
