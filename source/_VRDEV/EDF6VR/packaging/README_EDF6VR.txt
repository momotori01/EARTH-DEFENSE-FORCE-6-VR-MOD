EDF6VR 3.0.2
=============================

CHANGES IN 3.0.2
- Fixed: the scope picture's position on several weapons.

CHANGES IN 3.0.1
- Sight line thickness: laser sights, throw guides (grenades, turrets) and
  vehicle aim lines are drawn thinner, at 0.1, 0.2 and 0.3 of the game's own
  width. Each can be set under Extra VR settings, Sight line thickness (1 is
  the game's own width).
- Fixed: the laser sight seen in a scope slid away while walking or rolling.
- Fixed: the scope crosshair tilted with your head; it now tilts with the gun.

CHANGES IN 3.0.0
New:
- Scopes. When you zoom, the magnified view is shown on the weapon and the
  rest of your view stays normal size:
  - in the scope, matching its shape;
  - on the Air Raider marker guns' own screens;
  - on a small blue holographic monitor above the sights of zoom weapons
    without a scope;
  - for the Fencer, on a green panel in front of you, looking where the
    zooming hand aims.
  EDF6 VR setting.exe, Scope zoom:
  - Native: sharp, costs frame rate while zoomed.
  - Digital: no frame-rate cost, blurrier.
  - Off: the old whole-view zoom.
- Vehicles: aim with your gun hand. Point the right controller and the
  vehicle's aim follows, as if you pushed the right stick; the stick still
  works. On or off for each kind of vehicle under Extra VR settings, Vehicle
  hand aim. The Depth and the Barga start off.
- Proteus two-seater: the other rider sits in the cockpit as their own
  soldier, with their own look and colours. Made from your game files with
  Crew figures, Make (about 2 minutes), or together with the HD textures.
- Lighter Kurul shots: the Kurul's shotgun pellets are drawn without the
  ring that swells around each one, so big Kurul fights stay smooth.
  - On by default. EDF6 VR setting.exe makes it from your game files when it
    opens, or when you press Make.
  - To turn it off: Extra VR settings, Lighter Kurul shots.
Fixes:
- White ground with HD textures on 23 maps, including mission 65. If you made
  HD textures, press Make under HD textures once; only the 24 broken files are
  made again.
- Fencer shield:
  - A shield in the left hand now guards where the left hand points. Before,
    it guarded where the right hand pointed.
  - A raised shield always comes up square in front of its controller.
Small fixes:
- The Brute's door gun no longer has up and down swapped.
- Proteus two-seater:
  - The information monitor no longer disappears.
  - The base of the rear lever no longer flickers.
- Cockpit hand levers sit further forward and are slimmer.
- EDF6 VR setting.exe scrolls when it is taller than your screen.
- Bundles EDF6MultiSlot 1.5.33. The only change is extra logging to find why
  8-player missions sometimes drop everyone.

CHANGES IN 2.1.8
- Fixed: at a Picture size above 1.25, hands and weapons turned grey and
  shook.

CHANGES IN 2.1.7
- Meta Quest over Quest Link / Air Link: double vision fix. Meta's own runtime
  shows each eye's picture only over that eye's field of view, and the wider
  picture was squeezed into it, shifted the opposite way in each eye. Each eye
  now gets exactly the part of the picture that covers its field of view.
  SteamVR and other runtimes are unchanged. Supported: SteamVR and Virtual
  Desktop. Meta Link may work too, but it has not been tested in detail. If
  you still see double, use SteamVR: in SteamVR, Settings -> OpenXR -> Set
  SteamVR as OpenXR runtime.

CHANGES IN 2.1.6
- Online: other players are shown where they really are. The base game put a
  player's position and facing into only some of the updates it sends, and at
  60 fps those were often skipped for seconds, so others could look metres off
  or face the wrong way. EDF6MultiSlot 1.5.32 sends them in every update. You
  can still play with people who do not have the mod.

CHANGES IN 2.1.5
- EDF6 VR setting.exe: one window for everything you set outside the game.
  Update, VR / normal mode, picture size, HD textures, gun hand, a log zip for
  problem reports, and extra VR settings (cockpit, compact HUD, aim mark size,
  recoil, shot vibration, desktop mirror, reset). It replaces VR_Play.bat,
  Set_Resolution.bat and HD_Texture_2x.bat, and deletes them the first time
  you open it. See SETTINGS PROGRAM below.

CHANGES IN 2.1.2
- Left-handed mode: Rangers, Wing Divers and Air Raiders can hold and fire
  the gun with the left hand. The trigger, grip, buttons and sticks all change
  sides with it. Fencers and vehicles stay right-handed. To switch it on,
  start the game once after updating, then set LeftHanded=1 at the very top of
  Mods/Plugins/EDF6VR.ini and restart. See LEFT-HANDED MODE below.

CHANGES IN 2.1.1
- Cockpit surfaces look better: new materials and textures.

CHANGES IN 2.1.0
- Update_EDF6VR.bat is included. Run it and it updates EDF6VR (with the
  bundled EDF6MultiSlot) to the latest release on GitHub. If you run into a
  bug, try this first. See UPDATE below. When a newer
  version is out, the lower-left line of the menu says so.
- Fencer: better controls, closer to the game on a monitor.
- Fencer: after a revive, your equipment no longer points the wrong way.
- EDF6MultiSlot 1.5.12. Known issue, being fixed: rarely, with certain players
  in the room, the host cannot press OK to start a mission. If it happens,
  restart the game.

CHANGES IN 2.0.3
- Fencer: spear thrusts go where the spear points. Since 2.0.0 the thrust
  followed the game's slower aim, so it missed to the side of the spear
  whenever the hand had just moved.

CHANGES IN 2.0.2
- Tanks (Blacker, Varias, EMC, Railgun, Titan, Kebler): the direction you
  drive is taken from the hull again; turning the turret no longer changes
  where the tank goes.
- EDF6MultiSlot 1.5.7: a more detailed log when the game crashes (see
  README_EDF6MultiSlot.txt). Rooms and joining are unchanged.

CHANGES IN 2.0.1
- Brute door gunner: easier to control. The gun starts pointing straight out
  of the door; stick up and down are inverted in this seat; held down, the
  barrel swings through straight down to the other side instead of stopping
  (let go or change direction and it steers as usual). The right-hand seat's
  screens are no longer mirrored.

CHANGES IN 2.0.0
- VR cockpits for every vehicle, built for each vehicle type into the
  machine's own hull. The front, sides and top stay open to the world; where
  the hull is cut away for the view, the cut is covered. Each cabin shows your
  armour, ammunition and radar on its own screens.
  - Walkers: Nix, Combat Wagon, Gravis, Depth Crawler, Barga, Proteus MK2
    (pilot and missile seats in tandem, and both gunners).
  - Tanks: one driver's cabin for every tank, plus the Titan's gunner pods.
  - Combat vehicles: Grape, Negling, Caliban.
  - Helicopters: Nereid, Euros, Heron, and the Brute's pilot and door gunners.
  - Trucks: the kei truck and the trailer truck from the driver's seat; the
    armed pickups get a built cab. Bikes are ridden in the open.
  - The robots' weapons (Nix, Gravis, Depth Crawler) kick when they fire.
  - VehicleCockpit=0 in the [VR] section goes back to the previous view.
- EDF6MultiSlot 1.5.6. With 8-player rooms ON, the room search lists 8-player
  rooms only; OFF lists ordinary and 8-player rooms. Switching while the room
  list is open searches again. Also a retry for players dropping out of a room
  together, and a thread-safety fix in the bundled winmm.dll (it changes too:
  extract the whole archive). See README_EDF6MultiSlot.txt.
- Fixes since 1.6:
  - Aim: no dead band around the hand; two-handed aim follows the palms, and
    one-handed points the same way.
  - Turning with the stick no longer shakes the held weapon and hands.
  - Hands come back after changing class; the open wrist is closed.
  - First person no longer drops to third person after eight or nine missions.
  - Vehicles: the view is level with the horizon.
  - Compact HUD: the reload gauge is no longer cut off.
  - Ranger: sights and throw guides start at the weapon in your hand and do
    not lean while you run or roll; dual wield shots during a reload go
    straight.
  - Wing Diver: aiming straight up no longer sends you off.
  - Air Raider: an ant carrying you off no longer throws the view onto the flat
    panel; the radio is no longer black with HD textures.
  - Fencer: hand weapons aim along the barrel, the flash comes out of the
    muzzle, and left-hand shots land on the reticle with their spread kept.
    Shoulder weapons and the shield stay steady through dashes and jumps; the
    shield is lowered at rest and comes up to guard while you hold its trigger.
- Updating: EDF6VR.ini is replaced once with the new defaults. Your resolution
  is kept and the old file is saved as EDF6VR.ini.v1.bak. If you built HD
  textures, run HD_Texture_2x.bat once more to repair the ones that went dark.
- Known: the Fencer's left shoulder aim line can swing during a dash and is
  slightly off at a jump. A fix for hands stretching after a revive is in but
  not yet seen in a real revive.

CHANGES IN 1.7.3
- Nix has a seated 3D cockpit, with armour on the left, ammunition on the right,
  a circular radar and translucent blue instrument panels. The cabin includes
  compact handgrips, pedals, a bucket seat and detailed metallic interior parts.
  The open upper radar rim and side windows keep the moving machine visible.
- Fixed the temporary flat-panel transition during Nix jumps and the severe
  frame-rate drop / persistent flat-panel view that could occur when boarding.
  The boarding fix was confirmed in gameplay. Cockpit initialization runs once
  per device/session; first use can still cause a brief loading hitch.
- VehicleCockpit=0 in the [VR] section restores the previous vehicle view after
  a restart. The new cockpit currently targets Nix, not every vehicle.
- Bundles EDF6MultiSlot 1.5.4-recovery1, an experimental response to connection
  loss during room entry. Its effect on group disconnections is not yet verified
  in multiplayer. For this group test use that same version for everyone.
  See README_EDF6MultiSlot.txt and HANDSHAKE_RECOVERY_JA.md.
- Existing VR, MultiSlot and mod-loader settings are preserved when updating.
  Cockpit on/off FPS comparisons have not yet been measured under equal conditions.

CHANGES IN 1.7.2
- First person no longer gives up part way through a run. After eight or nine
  missions without quitting the game, the view dropped back to third person
  and stayed there until you restarted. The mod keeps a small table of the
  cameras it has taken over, and the camera the game throws away at the end of
  a mission was never letting go of its place, so the table filled up. Three
  players' logs hit it at mission nine.
- Your hands come back after changing class. Once you had switched soldier,
  the hand models were gone for every class for the rest of the session, and
  the fingers of the new soldier's skeleton were being read under the names of
  the old one's.
- Vehicles look at the horizon. Sitting in a Nix or a Vegalta you faced some
  thirty degrees into the ground, because the game aims its own camera down at
  the machine from behind to frame it, and that tilt came along when the view
  moved into the cockpit. The heading and the lean on a slope are unchanged.
  VehicleLevelView=0 in the INI puts the old behaviour back.
- Fencer: a hand weapon no longer points off in a wrong direction now and
  then. Whether a weapon is carried or shouldered is worked out from the arm
  bones, and when that lookup came up empty on the first frames of a new
  soldier the answer was kept for the rest of that life.
- Air Raider: being bitten and carried off by an ant no longer throws the view
  onto the flat panel. The mod watches for the game's own aerial targeting
  camera, which pulls tens of metres away, and it recognised it by that
  distance alone -- so an ant that ran off with you and threw you about looked
  the same. It now also asks that you still have a body to measure against and
  that the camera is above you, looking down, the way targeting does.
- EDF6MultiSlot 1.5.3 included, updated from 1.5.1. Its log now says whose
  log the file is, who was in the room with you, and whether the game was
  closed properly or fell over -- all of it for tracking down players being
  dropped out of a room. Nothing of it is written to the game.
  See README_EDF6MultiSlot.txt.

CHANGES IN 1.7.0
- EDF6MultiSlot 1.5.1 included, updated from 1.4.3:
  - Eight-player online co-op now works properly. It only takes effect when
    YOU are the host: unless you switch it on and make an eight-player room,
    ordinary online play is untouched.
  - You can copy the armour of the lowest player of your own class in the
    room, so that playing alongside a beginner, or on a class you rarely
    use, does not leave you far behind in health. There is a cap per class,
    and it does nothing while the gap is 300 or less.
  - Various bug fixes.
  See README_EDF6MultiSlot.txt for how to turn these on and for the rest.
- The VR side is unchanged from 1.6.4.

CHANGES IN 1.6.4
- Fencer: a shield no longer flips a quarter turn now and then. It kept its
  orientation by snapping the model onto whole axes, and a shield is a plate
  whose broad axes are close enough for that choice to swap, so it braced
  forward one moment and lay flat to the side the next. Worth more than
  looks: a block is decided from where you are AIMING, so a shield that
  looks wrong leads you to turn your reticle away from what is hitting you.
- Fencer: your weapons come straight back after an ant picks you up and
  shakes you. Whether each weapon is carried in a hand or sits on a shoulder
  is decided once and was not being decided again after that, so the wrong
  one stuck until you changed weapons.
- EDF6MultiSlot 1.4.3 included, updated from 1.2.5. It adds matching your
  armour to the lowest in the room (F4 or left stick click, in a room) and
  the 8-player toggle on the left stick as well as F2. Note: F4 is also this
  mod's frame-dump key, but only while [Diagnostics] DevKeys=1, which ships
  off. See README_EDF6MultiSlot.txt for the rest.
- Known: after a revive a hand model can stretch back toward the body until
  the mission ends. It is understood and being worked on.

CHANGES IN 1.6.3
- Ranger: the thrown-weapon guide no longer leans while you run or roll. The
  game draws that parabola with your own movement added to the throw, and
  the thrown object does not take it, so the line drifted off to one side
  while the throw itself went where you aimed. The line and its landing
  marker now show the throw alone. Same for the laser sights and the
  weapons you hold, where the same lead was small enough to go unnoticed.
- The sub weapon keeps drawing its guide from the body, where the game has
  it, and it aims with your right hand as before.

CHANGES IN 1.6.2
- Ranger: the laser sight and the thrown-weapon guide now leave the weapon
  in your hand instead of the model on your body. The right hand keeps the
  game's own direction, so the line still shows where the shot goes; a
  weapon in the left hand is aimed along that hand. The landing marker
  follows the line, and neither is pulled about by a roll any more.
- Ranger dual wield: a left-hand weapon fired while the right one is
  reloading no longer shoots off to one side. The reload animation was
  leaning that weapon's shots away from the hand; the shot is put back on
  the hand it left, keeping the weapon's own elevation and its spread.
- EDF6MultiSlot 1.2.5 included, updated from 1.2.3 (see
  README_EDF6MultiSlot.txt for its own changes).
- The sub weapon's guide stays as the game draws it, from the body: that
  weapon is held in neither hand.

CHANGES IN 1.6.1
- Fixed: in the compact HUD the reload gauge under the weapon boxes was cut
  off for the Ranger and the Air Raider.

CHANGES IN 1.6.0
- Fencer: the two weapons aim independently. The right hand is the game's own
  aim; the left hand gets a second aim of its own, driven the same way, with
  the same heaviness and turn speed and its own weapon's recoil. A second
  reticle shows where the left weapon points (a Ranger's left gun in dual
  wield gets one too). Melee attacks and shields follow the hand that holds
  them.
- Fencer: hand-held weapons (cannons, gatlings, blades, hammers, spears,
  shields) ride the controllers, trailing them with the aim's heaviness, and
  roll with the controller the same way. Shots, muzzle flash and the weapon's
  own animations (reload, spear thrust, shield deploy) come with them.
  Shoulder weapons (mortars, missile pods) sit on your shoulders, anchored to
  the headset, and only turn to their aim; their shells, muzzle flash and
  trajectory guide come from the weapon on your shoulder.
- Hands: the Fencer now has hand models at the controllers too, with the same
  finger movement as the other classes.
- Nameplates are only drawn smaller while VR is on; on the desktop (VR not
  started or turned off with F11) they keep the game's own size.
- EDF6MultiSlot 1.2.3 included (see README_EDF6MultiSlot.txt for its changes).
- INI [VR]: FencerSplitAim, FencerHeavyAim, FencerHandWeapons,
  FencerHandOutward/Ahead/UpMetres, FencerHandRoll, FencerShoulderOnHead,
  FencerShoulderSide/Down/BackMetres, FencerSwapHands.

CHANGES IN 1.5.0
- Compact HUD: the radar, armour bar and weapon/item boxes are taken off the
  big panel and drawn smaller, close together, at the panel's bottom-right
  corner -- or on the inside of a wrist. Left controller beside your head,
  stick DOWN for 3 seconds moves it: panel corner -> right wrist -> left wrist.
  Subtitles, chat and mission messages stay where they were. Layouts for all
  four classes. [Render] UiCluster=0 restores the old panel; UiCluster*Scale,
  UiClusterRight/Bottom and UiClusterWrist* adjust sizes and places (live).
- Other players' nameplates, squad health and rescue prompts appear in the
  world over the player they belong to and are seen through walls, instead of
  on the HUD panel. Nameplates are drawn smaller (they were laid out for a flat
  screen); [Render] WorldNameplateSize sets the size (1.0 = the game's own),
  WorldNameplates=0 restores the panel.
- Updating is now just extracting over the old install: EDF6VR.ini is no
  longer in the package, so your settings are kept and new ones are added.
- Fixed: the reticle did not appear when Set_Resolution.bat was set to High
  (1.25) or any size above about 1.2.
- Fixed: a Ranger's left gun could fire straight up, right after the guns were
  swapped (the shot was aimed through the other gun's frame).
- The menu board (title, menus, loading) stays put in the room instead of
  following the headset. It is placed in front of you when it appears; F12
  or the left-temple stick-UP hold brings it back in front of you.
  [Render] BoardWorldLocked=0 restores the old behaviour.

CHANGES IN 1.4.2
- Recoil: each shot kicks the weapon and the hand holding it up and back;
  sustained fire keeps it shaking. Only the picture moves, not the aim.
  [VR] RecoilKick, RecoilPitchDegrees, RecoilBackMetres, RecoilDecayMs.
- Firing vibration goes to the hand that fired, and to both hands while the
  left hand steadies the weapon. The left gun of a dual wield now vibrates
  too ([VR] ShotBuzz). Hits and explosions still reach both hands.

CHANGES IN 1.4.1
- Fixed: preset chat text sat six lines below its bubble (the game scaled the
  text's height for a 16:9 rectangle the headset fit does not have).
- Fixed: after a Ranger dual-wield toggle the left trigger could stay dead
  for the game (jump, key config) and the index finger stay bent: a trigger
  that rests slightly above zero was never seen as released.
- Fingers: the trigger finger closes at a light pull, the grip fingers follow
  the grip from first touch and are a fist at a light squeeze; the thumb
  moves a little onto the stick instead of sweeping to the palm.
- New [Render] BoardWidthMetres / BoardDistanceMetres size the menu board;
  UiPanelWidthMetres / UiPanelDistanceMetres defaults are 2.3 m at 1.7 m.
- EDF6MultiSlot 1.1.0 included (see README_EDF6MultiSlot.txt for its changes).

CHANGES IN 1.4.0
- Up to 8 players online: EDF6MultiSlot 1.0.0 is included. F2 on the room
  menu switches it on and off (shown bottom-left). Hosting with it ON, only
  players who have the mod can join; as a guest it does not matter. Up to
  4 players nothing changes; from the 5th player on, enemy numbers grow with
  each extra player, and enemy stats are capped at the 5-player level.
  Room screen: F3 / Tab / right-stick click shows members 5-8. Details in
  README_EDF6MultiSlot.txt.
- Hands: Ranger, Wing Diver and Air Raider now have hand models at the
  controllers, cut from the soldier's own model. Fingers rest half open;
  grip closes the outer three, trigger the index finger, the thumb comes
  down when it touches the stick. See HANDS below.
- Weapon and hand placement: beside the left temple, hold the LEFT stick
  RIGHT for three seconds to adjust the weapon, LEFT to adjust the hand.
  Stick DOWN no longer enters. Hand placement is saved per controller type
  ([Hand.index], [Hand.touch] ...) and switches automatically.
- Ranger dual wield: the holster is also the whole back, shoulder width,
  down to the shoulder blades. Reach over either shoulder.
- Development keys F2-F4, F6-F8 and F10 are off unless Diagnostics/DevKeys=1,
  so other mods can use them. F5, F9, F11, F12 unchanged.

CHANGES IN 1.3.3
- Fixed: textures made by HD_Texture_2x.bat were not used, because the
  included ModLoader.ini turned file redirection off.
- ModLoader.ini is no longer included, so your own loader settings for other
  mods are kept. HD_Texture_2x.bat turns Redirect on if it is off.
- Fixed: a square in the middle of the HUD was blank, hiding MISSION CLEAR
  and online chat bubbles. The game's own crosshair is now switched off
  instead while the VR reticle is shown.

CHANGES IN 1.3.2
- Fixed: the game froze every few seconds when it was drawn by a different
  graphics card than the headset uses. VR still cannot start in that case;
  see the log line "VR: not retrying" for what to change.

CHANGES IN 1.3.1
- Fixed: the held weapon could start wobbling, dragged along by the body
  model, and stay that way for the rest of the session. A single frame in
  which one eye missed the weapon used to switch the weapon layer off for good.
- Fixed: the view switched to the flat panel when you looked around while
  knocked down or blown away.

CHANGES IN 1.3.0
- HD_Texture_2x.bat makes the game's colour textures twice as sharp: walls,
  roads, ground, signs, shop fronts, weapons and enemies. Off until you run it
  and answer y. It builds them from your own installation; nothing from the
  game is redistributed and no game file is written. See HD TEXTURES below.
- Set_Resolution.bat changes the picture size without editing the INI. The
  number is a multiple of normal, so 1.25 is a quarter sharper.
- The Ranger's dash goes where you are looking instead of where the controller
  is pointing. The weapon and the reticle stay with your hand.
- EDF6VR.log keeps the last ten runs instead of growing forever.

CHANGES IN 1.2.0
- Audio tearing largely fixed. Your own sounds are placed where they belong:
  a shot at the weapon that fired it, footfalls beneath you, the booster at
  your back, and anything that happens at a place stays there. Volume, pitch
  and voice lifetime are unchanged. SoundPositionFix=0 turns it off.
- The weapon no longer disappears while rolling.
- The laser sight follows the gun again, including the left-hand gun while
  dual wielding.
- Roughly 20-30% more frames per second. In exchange, the soldier shown
  beside the mission menu is stretched.
- Known issue: a sentry gun firing at very close range can have its sound
  come from your held weapon.

HEADSET FIELD OF VIEW
The rendered field of view is set automatically from your headset, using the
headset from the previous launch (saved beside the INI as EDF6VR.fit). A
headset it does not yet describe falls back to 16:9, which is wider than any
headset needs, so nothing is cut off. A black edge means the saved field
belongs to a different headset: restart the game once with this one.
MatchHeadsetFov=0 turns it off.

RAISING THE RESOLUTION
ForceWidth and ForceHeight in the INI are the sharpness control. The fit takes
its pixel density from them and then spends it only inside the lenses, so
raising them no longer wastes most of it on field of view nobody sees. Scale
both by the same factor, keeping the 16:9 shape; the fit decides the final
rectangle. Maximum 7680x4320. Lower them the same way if the frame rate is
short. On a 97 by 97 degree headset, for example:
  3840x2160 (default)  ->  renders 2336x2160,  5.0 Mpix
  4800x2704            ->  renders 2912x2704,  7.9 Mpix   (+56%)
  5760x3232            ->  renders 3488x3232, 11.3 Mpix  (+123%)
  7680x4320            ->  renders 4656x4320, 20.1 Mpix  (+299%)
The RESOLUTION line in EDF6VR.log reports what was actually locked in.

Picture size in EDF6 VR setting.exe does the same edit for you. Pick Low,
Normal or High, or type your own number: it is a multiple of the normal size,
so 1.25 is a quarter bigger and 0.8 is a fifth smaller. The limit is 2.0.

HD TEXTURES
HD textures in EDF6 VR setting.exe makes the game's colour textures twice as
sharp. It is off until you press Make there. The window shows how far it has
got; closing it stops the work, and Make goes on from where it stopped.

It reads your own installation, enlarges the textures and writes new copies
into the Mods folder. Your game files are never opened for writing and never
change, so there is nothing to back up and nothing that can be damaged.

  What it changes   Colour textures between 128 and 2048 pixels: building
                    walls, signs, ground, road surfaces, weapons, enemies.
  What it leaves    Normal maps, masks, ambient occlusion and the other
                    channels a shader reads as numbers. Enlarging those is not
                    a smaller benefit, it is wrong.
  Cost              About 300 MB of video memory in a mission, and roughly
                    30 GB on disk for the city maps. After the first run the
                    game starts and loads as it always did.
  Needs             A Vulkan-capable graphics card with 10 GB or more.

TURNING HD TEXTURES OFF
Press Delete beside HD textures in EDF6 VR setting.exe. The game is back to
normal the next time you start it.

It removes exactly the files it wrote, which are listed one per line in
Mods\HDTextureWork\written.txt. They are not in a folder of their own because
they cannot be: the mod loader reads a replacement only from the same path the
game asked for, so a new road texture has to sit at Mods\MAP\<the map>.RAB.
That folder is shared with any other mod you have installed, which is why the
list exists rather than a "delete this folder" instruction.

If you would rather do it by hand, delete the files named in that list, then
delete Mods\HDTexture and Mods\HDTextureWork. Deleting Mods\HDTextureWork on
its own only frees working space; it does not turn the textures off.

HD textures are not a VR feature. Choosing Normal in VR mode only renames the VR
plugin; the mod loader and its replacement mechanism are untouched, so the game
keeps the sharper textures in flat mode as well. Flat mode also renders one
ordinary screen instead of two 4K eyes, so it uses less video memory than VR
does with the same textures.

Nothing from the game is redistributed: every texture is made on your machine
from your own copy. The three programs it uses are in Mods/HDTexture with
their licences.

CHANGES IN 1.1.0
- Ranger-only dual wield: draw or holster the other primary weapon with LEFT
  grip beside the LEFT temple. Aim and fire independently with both hands.
- One vibration on entering the holster area; native weapon-change sound on
  each accepted draw/holster toggle. Both weapon models render in both eyes.
- Reloading pauses for both guns during dual wield. Zoom and two-hand aim are
  disabled. Sustained fire adds independent, continuously recovering spread.
- Movement remains available beside the left temple, during view-height reset
  holds, and during weapon placement adjustment.
- Left muzzle flashes follow the left gun, including Blazer's effect.
- Keeps the draw-distance and vibration improvements from 1.0.1.

CHANGES IN 1.0.1
- Restores normal game draw-distance / LOD scaling for distant buildings in VR.
- Fixes missing controller vibration during head-side stick holds, with
  continuous feedback while holding and cancellation when interrupted.
- Adds the view-height reset: LEFT controller beside the LEFT temple,
  LEFT stick UP continuously for THREE seconds. See VIEW HEIGHT RESET below.
- Audio crackling remained a known issue in 1.1.0; 1.2.0 addresses it.

INSTALL
1. Install and run the Steam PC version of EARTH DEFENSE FORCE 6 normally once.
2. Close the game. In Steam: Manage -> Browse local files.
3. Extract ALL contents of this ZIP into the folder containing EDF6.exe.
   EDF6 VR setting.exe and winmm.dll must sit directly beside EDF6.exe, not in
   a nested folder.
4. Connect your headset and controllers to your PC VR runtime (see below).
5. VR is on as installed. To play on the monitor instead, open
   EDF6 VR setting.exe, choose Normal under VR mode and press Apply. This only
   saves your choice; it does not launch the game.
6. Launch EDF6 from Steam normally. Open the settings program again only to
   change modes or settings.

No Python, Visual Studio or separate OpenXR loader DLL is required.
The bundled EDFModLoader needs the Microsoft Visual C++ x64 runtime normally
installed with Steam games. If Windows reports MSVCP140 / VCRUNTIME140 missing,
install Microsoft's supported x64 redistributable:
https://aka.ms/vs/17/release/vc_redist.x64.exe

If other mods are already installed, BACK UP winmm.dll before extracting.
This package includes the mod
loader (winmm.dll) but no ModLoader.ini, so existing loader settings are kept;
without that file the loader uses its defaults. This bundle includes EDF6MultiSlot
and EDF6ClearLoot as listed above. It includes no Patcher, game executable or CPK
archives, and does not remove existing mods.
Making HD textures needs Redirect=True in ModLoader.ini and sets it if a
ModLoader.ini says False; nothing else in that file is changed.

SETTINGS PROGRAM (EDF6 VR setting.exe)
EDF6 VR setting.exe sits next to EDF6.exe. Open it with the game closed to
change anything outside the game. Each item has its own button, and changes
are used the next time you start the game. Its buttons are off while the game
runs.
Main tab:
  Update          The installed and the newest version. Update now installs
                  the newest release from GitHub; your settings are kept.
  VR mode         VR (headset) or Normal (monitor, no VR).
  Picture size    Low 0.8, Normal 1.0, High 1.25, or your own number from 0.5
                  to 2.0. See RAISING THE RESOLUTION.
  Scope zoom      Native (sharp, costs frame rate while zoomed), Digital (no
                  cost, blurrier) or Off (the old whole-view zoom).
  HD textures     Make or Delete the 2x textures. See HD TEXTURES.
  Crew figures    Make or Delete the other rider of the Proteus two-seater
                  (about 2 minutes). Making HD textures makes them too.
  Gun hand        Right or Left. See LEFT-HANDED MODE.
  Problem report  Puts the logs and settings into one zip, saved in the game
                  folder next to EDF6 VR setting.exe as
                  EDF6VR-logs-<date>-<time>.zip. Send it with a bug report.
Extra VR settings tab:
  VR cockpit, vehicle hand aim (on or off for each kind of vehicle), compact
  HUD (on or off, and where: the corner of your view or the right or left
  wrist), aim mark size, sight line thickness (laser sights, throw guides and
  vehicle aim lines, each on its own), recoil, shot vibration, desktop mirror,
  lighter Kurul shots, and Reset: every VR setting back to how it came. Reset keeps your
  picture size and gun hand, and keeps the old file as
  Mods/Plugins/EDF6VR.ini.reset-<date>-<time>.bak.
  The window scrolls when it is taller than your screen.
The first time you open it after downloading the ZIP with a web browser,
Windows may say "Windows protected your PC": choose More info, then Run
anyway. The program is not signed. Everything it changes can also be set by
hand in Mods/Plugins/EDF6VR.ini.

UPDATE
From 2.1.0 on: close the game, open EDF6 VR setting.exe and press Update now
(or double-click Update_EDF6VR.bat in the game folder). It compares your
version with the latest release on GitHub, checks the download, and replaces
only the files that changed (the old ones are kept in EDF6VR\backup). Your
settings are kept, and if you play flat (Normal) it stays flat.
By hand, or from an older version: close the game and extract the new ZIP over
the old install, replacing files. That is all. Your settings are kept: the package no longer contains
Mods/Plugins/EDF6VR.ini. On start the mod creates it if it is missing, and on
an existing one only adds settings that are new in this version, with their
default values. The shipped defaults are in EDF6VR/EDF6VR.defaults.ini for
reference. HD textures and EDF6MultiSlot.ini are not in the package either,
so they stay as they are.
If you play flat and extracted an update by hand, choose Normal under VR mode
again: the new EDF6VR.dll takes precedence over the disabled one.

VR / FLAT SWITCH
VR mode in EDF6 VR setting.exe:
- VR enables Mods/Plugins/EDF6VR.dll. VR starts automatically when the runtime is ready.
- Normal renames ONLY that DLL to EDF6VR.dll.disabled so none of this VR plugin
  loads. Other installed plugins are unaffected. Your settings are preserved.
- Close the game before switching; the buttons are off while it runs.
- The choice persists for later launches from Steam as well.
- F11 is an in-session VR toggle, NOT a complete unload. Use Normal for ordinary flat play.
- If a package update leaves both an enabled and disabled DLL, the enabled one
  takes precedence. Switching to Normal keeps the old disabled copy as .previous-*.

HEADSETS / RUNTIMES
The mod uses OpenXR and automatically uses XR_RUNTIME_JSON if explicitly set,
otherwise the active 64-bit OpenXR runtime registered on your PC. It does not
change the system runtime, install drivers, or guess which of several installed
headsets you intend to wear. Choose the runtime for your current connection once.

- Valve Index / Bigscreen Beyond with Index controllers:
  Start SteamVR, connect devices, and select SteamVR as the OpenXR runtime in
  SteamVR settings (OpenXR; on some versions under Advanced/Developer).
- Quest through Virtual Desktop (wireless PCVR):
  Install Virtual Desktop on the headset and Virtual Desktop Streamer on the PC.
  Connect to the PC first. Select VirtualDesktopXR / VDXR as the OpenXR runtime
  in the Streamer settings. VR mode must be VR. SteamVR is not required
  for this route. Steam itself still launches EDF6. Confirm Runtime: VDXR in
  Virtual Desktop's performance overlay.
- Quest through Steam Link / SteamVR: use SteamVR as the active OpenXR runtime.
- Quest Link / Air Link: connect Link first and select Meta Quest Link's OpenXR
  runtime, or use SteamVR. Meta's runtime is not the Virtual Desktop route.
  Supported: SteamVR and Virtual Desktop. Meta Link may work too, but it has
  not been tested in detail.
  SEEING DOUBLE on Meta Quest? Use SteamVR: in SteamVR, open Settings -> OpenXR
  and press "Set SteamVR as OpenXR runtime", then play through SteamVR (Steam
  Link, or Quest Link with SteamVR). EDF6VR.log names the runtime it used on the
  "runtime negotiated" line.

The runtime selects the controller bindings: Valve Index or Oculus/Quest Touch.
Touch-compatible profiles are used for Quest controllers; no hand-tracking-only
or controller-free mode is provided. The mod reads per-eye FOV and IPD from the
runtime. No fixed Beyond IPD or vendor-specific performance preset is imposed.
Runtime, headset and active left/right profiles are reported in EDF6VR.log.
Use the same GPU for EDF6 and the VR runtime.

TEST STATUS: Bigscreen Beyond 2 + SteamVR + Index controllers has been played
on the development PC. Quest / VDXR and Quest Link use the supported OpenXR
paths and Touch bindings, but HAVE NOT been hardware-tested for this release.
Do not treat the automatic profile selection as a guarantee of identical
performance on every headset. Start with one short offline mission.

NORMAL CONTROLS
The mod feeds ordinary Xbox-style gamepad buttons to EDF6. The game retains
its own button assignments; the table below describes the emitted buttons.

Physical control                         Gamepad input / special action
Left stick                               Move (HMD horizontal heading basis)
Right stick left/right                   Turn
Right stick UP, on foot                  Y button
Right stick DOWN, on foot                L3 / left-stick click
Left / right trigger                     LT / RT
Left / right grip                        LB / RB (Index squeeze or Touch grip)
Right A                                  A (normally confirm)
Right B                                  X
Left A on Index / X on Quest              B (normally cancel)
Left B on Index / Y on Quest              Y
Left / right stick click                 L3 / R3
Quest left Menu button                   Start / pause, when exposed by runtime

Use the RIGHT controller to aim; the headset looks independently. Bring the
left hand near the front support point of the weapon to engage two-hand aim;
move it away to return to one hand. In normal single wield, reload works as in
the game: there is no manual magazine grabbing or reload gesture.

LEFT-HANDED MODE
Rangers, Wing Divers and Air Raiders can hold the gun in the LEFT hand. Choose
Left under Gun hand in EDF6 VR setting.exe and press Apply. By hand, it is at
the very top of Mods/Plugins/EDF6VR.ini:
  [LeftHanded]
  LeftHanded=0          1 = left-handed, 0 = right-handed (default)
  LeftHandedSticks=1    0 = keep the sticks where they are
Restart the game after changing it. While it is on, everything above swaps
sides: the LEFT trigger fires, the buttons and grips trade places, and you move
with the RIGHT stick and turn with the LEFT. The gestures below use the other
hand, and a Ranger draws the second gun from behind the RIGHT shoulder.
Fencers, vehicles and menus stay right-handed.

RANGER DUAL WIELD
This Ranger-only feature recreates alternating fire between two weapons in VR.
1. Bring the LEFT controller beside the LEFT temple, or anywhere behind your
   back within shoulder width, from head height down to the shoulder blades
   (reach over either shoulder). One short vibration signals entry; do not
   press against the headset.
2. Press LEFT grip once to draw the other primary weapon into the LEFT hand.
   Repeat the gesture to holster it and return to single wield. Each toggle
   plays the normal weapon-change sound. Holding grip does not repeat toggles.
3. Aim each gun independently. LEFT trigger fires the LEFT gun and temporarily
   overrides its usual action. RIGHT trigger fires the RIGHT gun with the
   normal game firing assignment. Grip inside the holster zone is reserved
   for dual wield; outside it the normal mapping is available after release.
- Both guns stop all reload progress while dual wielding, even when empty.
  Return to single wield to resume normal reloading of the equipped weapon.
- Two-hand support aiming and zoom are unavailable during dual wield.
  Entering dual wield while zoomed cancels zoom. No separate left reticle is shown.
- Repeated shots build spread separately for each hand, increasing in proportion
  to the accumulated recoil up to 20 extra degrees. Slower fire accumulates less
  recoil naturally; there is no special protection for low-rate weapons.
  Every shot's contribution fades continuously; stopping fire clears recoil
  within one second. The first shot after full recovery uses native accuracy.
- This uses actual shots, not the combined rate of both hands. Low-rate weapons
  can be fired alternately without the other hand adding to their recoil.
- You can move while reaching the holster. Grip takes priority over pending
  left-stick reset/adjustment holds. Finish active placement editing before
  using the dual-wield toggle. Other classes do not gain this feature.

Fencer: each weapon aims with its own controller. The right hand is the game's
own aim; the left hand has a second aim driven the same way, with the game's
heaviness, turn speed and recoil. Hand-held weapons ride the controllers with
that lag; shoulder weapons sit on your shoulders (headset-anchored) and only
turn. FencerSplitAim=0 in EDF6VR.ini restores one aim for both; the other
Fencer keys are described in the INI.
Vehicles: steering and aiming use normal gamepad sticks. Right-stick UP/DOWN
returns to analog vertical input instead of Y/L3. HMD movement is camera-only
while seated. The head-side gesture described below remains available.

HEAD-SIDE GESTURE (SECONDARY INPUT LAYER)
This right-hand gesture also works in menus and during staff credits, for
Start/Menu (including supported skip prompts), Back/View and D-pad chat input.
Move the RIGHT controller beside either temple, close to the side of the head
and not out in front. A short vibration indicates entering/leaving the zone.
While there:
- Right stick becomes the D-pad (up/down/left/right).
- Left stick click becomes Back/View.
- Right stick click becomes Start/Menu (use this to pause on Index).
Move your hand away to restore normal controls. Keep the controller near,
not physically against, the headset. The zone is relative to head orientation.

WEAPON / HAND POSITION AND ANGLE ADJUSTMENT
1. In a safe place, hold the LEFT controller beside either temple.
2. Hold the LEFT stick RIGHT continuously for THREE seconds to adjust the
   WEAPON, or LEFT for three seconds to adjust the HAND model. Continuous
   vibration confirms the hold is being received. Releasing the stick or
   leaving the temple before three seconds cancels it; nothing is changed.
3. Both controllers vibrate. Release both sticks and triggers to begin editing.
4. Left stick left/right: shift weapon sideways. Up/down: forward/backward.
   Right trigger: raise. Left trigger: lower.
   Right stick left/right: yaw. Up/down: pitch.
   Right/left grip: roll in opposite directions.
5. Right A: SAVE and exit. Left A (Index) / left X (Quest): CANCEL and exit.
   Clicking BOTH sticks resets the placement to its default; save to keep it.

Movement stays active during adjustment; other normal gamepad actions are
reserved for editing. The game itself is not paused.
Weapon placement is global, not a separate preset for every weapon.
Hand placement is saved per controller type the runtime reports ([Hand.index],
[Hand.touch] ...) and the matching set is used automatically; the shipped
values were found on PSVR2 Sense (reported as Index) and Quest controllers.
Settings are saved to Mods/Plugins/EDF6VR.ini.

HANDS
All four classes show their own gloved hands at the
controllers, from the wrist forward, taken from the soldier's own model. The
fingers rest half open; the grip closes the outer three, the trigger closes
the index finger, and the thumb comes down when it touches the stick. Hands
are drawn in the same layer as the weapon, so fingers and grip overlap
correctly. HandModels=0 in EDF6VR.ini turns them off; the other Hand keys are
described in the INI. Vehicles are unchanged.

VIEW HEIGHT RESET (LEFT TEMPLE + LEFT STICK UP)
Move the LEFT controller beside the LEFT temple, close to the headset without
pressing against it, and hold the LEFT stick UP continuously for THREE seconds.
A vibration confirms the reset. Release the stick before repeating.
Continuous vibration during the hold confirms input. Keep holding: releasing
the stick or leaving the temple before three seconds cancels the reset.
This resets only the tracked height baseline to the normal camera height; it
does not change facing direction or horizontal position. Lowering/raising your
head still adjusts camera height relative to the new baseline.
Movement stays active while entering the area and during the three-second hold.

RETICLE / DISPLAY
- [Render] MuzzleFlashScale=0.5 reduces the local Ranger's standard muzzle flash
  geometry to 50%. Use 1.0 for its original size (range 0.1 to 1.0).
  Save while playing; it updates in about 5 seconds. This does not change bullets,
  weapon size or NPC flashes. Other classes, flash types and dynamic light intensity are unchanged.
- Lock-capable weapons show markers at the game's target positions; ordinary
  weapons use a common VR reticle. Not every original weapon HUD design is recreated.
- In Mods/Plugins/EDF6VR.ini, [Render] ReticleScale controls marker size.
  Default 0.5; try 0.7 for larger. Save while playing; it updates in about 5 seconds.
  Valid range: 0.05 to 4. This changes size, not aim or the game's lock-on range.
- Desktop mirror includes the right-eye world, weapon and UI. DesktopMirrorFov=90
  crops it for streaming. Change this before restarting; it does not change HMD FOV.
- Game rendering is fixed at 3840x2160. Do not change resolution during VR play.
- Anti-aliasing is OFF by default (the optional filter softens the image).
- The in-mission 60 FPS limiter is removed. This is not a promise of 90 FPS.
  Set [Render] RemoveFpsLimit=0 before restarting if you want the original cap.
  Native weapon logic includes update-count-based reload and firing timers.
  All weapon timings and online synchronization above 60 FPS have not been
  comprehensively validated. Rendering FPS and simulation updates are distinct;
  the mod does not add a general high-FPS gameplay timing correction.

USEFUL KEYBOARD KEYS (game window focused)
F11: VR on/off for this session. F12: recenter head orientation and room reference.
F9: first-person diagnostic toggle. F2-F4, F6-F8 and F10 are development keys,
off unless Diagnostics/DevKeys=1 in EDF6VR.ini, so other mods may use them.
F1: toggle the separate Clear Loot mod while in a mission (game focused).
    Enabled by default. Turning it on plays the native item pickup cue once.
    The choice is saved in Mods/Plugins/EDF6ClearLoot.ini across restarts.
    This mod collects remaining drops near the mission-clear banner; not on retreat.
    It remains enabled in non-VR mode too. VR mode only switches EDF6VR.
    See README_ClearLoot.txt. Anti-aliasing remains off; use SceneAA in the INI if needed.
F5: FPS limiter toggle.

KNOWN LIMITATIONS / TROUBLESHOOTING
- Ranger and Fencer: movement, turning and lateral jumps/dashes may cause
  crackling or interrupted sound. This remains unresolved and can also occur
  after F11 turns VR off. There is no confirmed workaround in this release.
- Ranger dual wield: holstering during Volcano-style automatic volleys has not
  been fully verified. Special-weapon compatibility is not guaranteed for every
  weapon. The native burst-cancellation policy is retained.
- For the tutorial at the start of a new game, use a normal game controller.
  Press F11 to temporarily turn VR off; press it again to return to VR.
- Prominence: its special upward firing animation can hide the weapon; rare
  temporary panel view transitions also occur. This is an accepted known issue.
- Wing Diver: the shot origin may lag slightly during fast flight.
- Air Raider: if an ant bites you at the moment an air strike's camera is
  moving out, that one strike is shown in stereo instead of on the flat
  panel. The next one is normal.
- Fencer: whether a weapon is carried in the hand or sits on the shoulder is
  decided in the first 1.5 s after it is drawn; if a hand weapon stays on the
  shoulder or a shoulder weapon comes to the hand, re-equip it.
- Changing resolution mid-session can cause visual artifacts. Restart the game.
- Briefly looking upward before VR starts is a known low-priority issue.
- If VR does not start: connect the headset to the selected runtime, keep EDF6
  focused, then press F11. Check Mods/Plugins/EDF6VR.log for runtime/hook errors.
  F12 recenters. If the view remains wrong, restart the game.
- Test-only instant vehicle requests, automated deployment/firing, scripted
  poses and firing traces are OFF. Normal request points remain in use; Ranger
  dual wield pauses reloads as described above.
- Binary hooks target the tested Steam EDF.dll (build 17055427,
  SHA256 0d5092393a1b9d8f1b75b0c93d8a8f65615a36a92c46e40349c2b66c733d0916).
  Other regions/updates have not all been tested. Unsupported hook profiles
  are refused; do not bypass those checks. Split-screen has not been validated.
- Multiplayer compatibility has not been comprehensively tested. No matching
  mod on other players is claimed to be required or sufficient for compatibility.

UNINSTALL
Close the game. If you made HD textures, turn them off FIRST: Delete beside
HD textures in EDF6 VR setting.exe. The textures are loaded by the mod loader, so removing winmm.dll
leaves thirty gigabytes in Mods that nothing reads any more. Harmless, but it is
a lot of disk to lose track of.

Then remove EDF6VR.dll / EDF6VR.dll.disabled, EDF6VR.ini, EDF6 VR setting.exe,
Update_EDF6VR.bat, Mods\HDTexture,
README_EDF6VR.txt and the EDF6VR support folder installed by this archive.
EDF6MultiSlot: remove Mods\Plugins\EDF6MultiSlot.dll, EDF6MultiSlot.ini and
README_EDF6MultiSlot.txt; it removes its own Mods\UI file when disabled first
(see its README).
Remove winmm.dll and ModLoader.ini ONLY if no other installed mod needs them.
No game executable or save file is included or patched on disk by this package.

CREDITS / RUNTIME INFORMATION
EDFModLoader by BlueAmulet: https://github.com/BlueAmulet/EDFModLoader
OpenXR by The Khronos Group: https://github.com/KhronosGroup/OpenXR-SDK
VDXR setup: https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki
Licenses and component notices: EDF6VR/Licenses and EDF6VR/THIRD_PARTY_NOTICES.txt
This is an unofficial mod, not affiliated with SANDLOT, D3 PUBLISHER, Valve or Meta.
