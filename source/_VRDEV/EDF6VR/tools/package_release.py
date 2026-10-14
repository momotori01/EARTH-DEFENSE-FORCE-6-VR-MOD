"""Build a drop-in archive from an explicit allowlist, never the game folder tree."""
from pathlib import Path
import configparser
import hashlib
import json
import subprocess
import zipfile
import argparse
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import stage_hdtexture
GAME = ROOT.parent.parent
VERSION = '3.2.9'
# The online co-op mod built in _MultislotDEV, taken from its own release ZIP
# unchanged (its own package.ps1 pins the same loader hash).
MULTISLOT_ZIP = GAME/'_MultislotDEV/release/EDF6MultiSlot-1.6.9.zip'
MULTISLOT_SHA = 'B88E15F753147555A3367679E79A3FADC0B4C6D6E156C4159B2BF13C8CA9E780'
MULTISLOT_DLL_SHA = 'BD0DA3B4F0859CAF14F015FD8D1EA56836690A914D13C55689AD2E23E202757F'
# The build actually tested for this package. 1.7.4 and 1.7.9 were private
# builds for playing with friends; 2.0.0 is the public release that follows.
# 2.0.2: the six tanks drive on their hull again (VehicleStickOnHull), on
# 2.0.1, with EDF6MultiSlot 1.5.7 (crash log); the tank fix is the cockpit
# session's F6F48A39 build with version strings changed.
# 2.0.3: Fencer spear thrusts follow the drawn spear (shot_origin.h, 3DFB7CDB
# tested on hardware 2026-09-28) with version strings changed.
# 2.1.0: Update_EDF6VR.bat; Fencer aim full stick from 5 degrees; shield/shoulder
# rest poses not learned while down; EDF6VR.log lines appended atomically; with
# EDF6MultiSlot 1.5.12 (its menu line announces a newer EDF6VR). Each change
# hardware-checked on 2026-09-29.
# 2.1.1: the cockpit session's cockpit and texture update (CC0 atlases, trucks,
# Barga), same EDF6MultiSlot 1.5.12.
# 2.1.2: left-handed mode (tried on hardware 2026-09-29 with the sticks kept;
# then sticks swapped by default and the settings moved to a [LeftHanded]
# section at the top of the INI, which the merge now inserts whole into an
# existing file -- unit-tested and dry-run on a real 2.1.1 INI), same
# EDF6MultiSlot 1.5.12.
# 2.1.5: "EDF6 VR setting.exe" replaces VR_Play.bat, Set_Resolution.bat and
# HD_Texture_2x.bat (driven end to end on 2026-09-29: settings, log zip, a real
# update of a fake 2.1.1 folder, VR mode, HD delete/make); stereo diagnostics in
# the log (GPU, EYECHECK, XRVIEWS; first run here: stereo looks right). Same
# EDF6MultiSlot 1.5.12.
# 2.1.6: EDF6MultiSlot 1.5.32 (other players' position and facing sent in every
# update; the multi session's release ZIP, hashes checked on 2026-09-30); the
# stereo check's up/down verdict no longer fires on walls or a tilted head.
# 2.1.7: a runtime that keeps each eye's own field of view (fovMutable false:
# Meta's PC runtime, double vision on Quest Link) gets that field and the part of
# the picture over it (eye_crop.h). On SteamVR the same crop, forced with
# EyeFovCrop=1 and toggled live, looked the same as before (2026-09-30); on Meta's
# runtime it is untested. Same EDF6MultiSlot 1.5.32.
# 2.1.8: the hand/weapon layers may use a sixth of the card's memory (1-3 GiB)
# instead of a fixed 1 GiB: Picture size 1.5 needs 1121 MiB, and was refused, so
# the hands turned grey and shook (fixed and confirmed on hardware 2026-09-30).
# 2.2.0: vehicle aim by the gun hand (a box a kind of seat in the settings
# program), the Proteus tandem's other rider drawn seated, slimmer palm levers,
# Brute door gun no longer inverted (the cockpit session, hardware-checked
# 2026-10-01); Fencer shield square in front of its controller when raised and
# a left shield guarding where the left hand points (block test 5957C0 hooked,
# guard on the left aim confirmed in the 2026-10-01 log); HD pack revision 3
# (texture arrays and cube maps left alone; the 24 archives a revision-2 pack
# got wrong are made again -- mission 65's ground confirmed). With
# EDF6MultiSlot 1.5.33 (logging only; hashes checked on 2026-10-01).
# 3.0.0 (the user: "v300", 2.2.0 never shipped): all of 2.2.0, plus scopes
# (eyepiece, Air Raider screens, holographic monitors, the Fencer panel; Native,
# Digital or Off from the settings program's Main tab; hardware-checked round by
# round to "ズームOK" on 2026-10-01) and the lighter Kurul shots (light_effects.py
# drops the stuck pellet's ring; made by the settings program, on by default,
# toggled live there on 2026-10-01). The settings window scrolls when taller
# than the work area. Same EDF6MultiSlot 1.5.33.
# 3.0.1 (the user: "v301"): sight line thickness per kind (laser sights, throw
# guides, vehicle aim lines; [Render] *Width, defaults 0.1/0.2/0.3 = the user's
# own settings; settings program row), the scope's camera takes the laser's own
# walk carry (LASERSCOPE offset 0.02 m walking and rolling, was one step), the
# scope reticle cants with the weapon. All checked on hardware 2026-10-01. Same
# EDF6MultiSlot 1.5.33.
# 3.0.2 (the user: "v302"): the scope picture placed right on several weapons
# (launchers on their left lens, shoulder launchers never on top, small top
# sights as lenses, the Laser Guide Kit's three panels), each checked by the
# user on textured model previews (research/scope) on 2026-10-02 rather than on
# hardware. Same EDF6MultiSlot 1.5.33.
# 3.1.0 (the user: "v310"): Fencer melee weapons laid on the hand, with the
# game's own swing while the attack animation plays; radio subtitles moved
# toward the middle of the panel (pieces checked against the game's layouts);
# quick chat bubbles drawn beside each player in the world, shrunk in VR about
# the speaker's head; the Fencer compact HUD from the game's layouts (charge
# and reload gauges); lowered shield on the controller; Power Blade sheath on
# the shoulder; the compact HUD steps aside for the chat window. Each checked
# on hardware 2026-10-02. With EDF6MultiSlot 1.5.34 (its logging fixed; ZIP
# and DLL hashes checked here on 2026-10-02).
# 3.1.5 (the user: "v005"): combat frames -- the walking bob smoothed out of the
# view, cockpit and head with it; the arms and shoulder weapons aim where the
# right controller points, set per machine on boarding (Nix and Eiren); the
# lower screen shows the radio subtitles alone (their own capture). Fixes: the
# headset's own recenter resets like F12; the Power Blade's swing no longer
# lands on the sheath. Each checked on hardware 2026-10-02/03. Same
# EDF6MultiSlot 1.5.34.
# 3.1.6: two-hand shots seen in their own directions by the other players
# (hand_aim_sync.h; online test 2026-10-04); HD textures off and on without
# deleting (settings exe box, hd_textures.py --off/--on; checked on the real
# pack); online menus on the board (menu_board.h; checked on hardware); Clear
# Loot 0.1.3 (the white/black text endings). With EDF6MultiSlot 1.6.6 (rooms
# kept apart from older versions; its README without the difficulty notes,
# the user's wish -- ZIP and DLL hashes checked here on 2026-10-04).
# 3.1.7 (the user: "おｋ上げて"): the crew-colour search asks before it reads
# (no first-chance exceptions, guard pages left armed; tested). With
# EDF6MultiSlot 1.6.7 (its exception log keeps fatal ones, seat and vehicle
# appearance logging; rooms as 1.6.6 -- ZIP and DLL hashes checked here on
# 2026-10-05, the rebuilt ZIP 7E9DC310).
# 3.1.8 (the user: "動作確認 / それじゃあまとめて"): the Ranger's left gun gets
# its reticle (published after the left hold command is built; checked on
# hardware, RETICLE aim2=1 while dual wielding); Fencer hand cannons fire along
# the barrel from the direction the game gave the accuracy cone (cone kept;
# checked on hardware). Vehicle hand aim on at first in the Nix only (the
# user: "ビーグルのコントローラエイム、ニクス以外は既定値でオフにしておいて"; an older
# INI's seats turned off once, VehicleHandAimDefaults=2; tested).
# EDF6MultiSlot 1.6.7 as in 3.1.7.
# 3.1.9 (the user: "マルチの修正をまとめてアップして ... 内容コレだけなら確認不要"): EDF6MultiSlot 1.6.8
# (player 5+ seat lists of the Caliban, Grape and trucks written out of range -> heap corruption; rooms as
# 1.6.6 -- ZIP and DLL hashes checked here on 2026-10-06, README diff = the update section only). The VR DLL
# also leaves an old vehicle-recoil state's node array alone (no play effect, not in the notes).
# 3.1.10 (the user: "まるちMODアプデしたからまとめて ... これだけでいい", then "アップはしないでね"):
# EDF6MultiSlot 1.6.9 (Air Raider credit share with 5+ players; its details only in the zip's README, the
# user's wish: "webで読める説明は不要") -- ZIP and DLL hashes checked here on 2026-10-06, README diff = the
# update section only. Versions compare numerically everywhere (Update-EDF6VR.ps1 [version], settings_core
# ParseVersion, MultiSlot VersionAt), so 3.1.10 is newer than 3.1.9.
# 3.1.11 (the user: "OK,まとめてアップして ... 内容はコレだけだから確認無しでアップしてOK"): Fencer shoulder weapons
# that fire straight ahead -- shells and guide line onto the hand's aim, not the post-shot barrel dip (shot_origin.h,
# guide_probe.h; tested). EDF6MultiSlot 1.6.9 as in 3.1.10.
# 3.1.12 (the user: "OK,それではまとめてアップして 修正内容は １０％程度のFPSの改善 確認は不要。アップして"):
# [Render] ShareEyeShadows=1 -- the right eye's shadow viewports keep the left eye's maps (cmd0 clear and cmd4
# batches dropped, native_renderer_probe.h); virtual headset 29.4 -> 26.7 ms, hardware shadow 3.2 -> 1.2 ms a
# frame, tested 2026-10-09. EDF6MultiSlot 1.6.9 as in 3.1.10.
# 3.1.13 (the user, after testing: "確認しました。下の判定範囲はもう少し狭くていいので、37度にしてください。他はOKです。
# アップ用にまとめて"): "Stick up / down buttons" in the settings exe ([VR] RightStickButtons), the up/down cone
# (40 up, 37 down; stick_buttons.h), the Laser Guide Kit's three panels as one picture (scope.cpp
# ScopeArrangeSurfaces), the "turned" event for missions 1 and 116 (first_person VrPlayerTurned), the Fencer guide
# line past 30 degrees (guide_probe.h). Then (the user after a heavy-stage sortie: "重さは感じるね exeで１か０を選べる
# ようにしておいて。規定は０ これも含めてまとめて"): the Air Raider's air-strike camera in the headset, [VR]
# AirStrikeCamera, 0 by default, "Air-strike camera" 2D / VR in the settings exe (plugin.cpp ApplyTacticalCamera).
# And (the user: "exe内でゲーム中でも変更できる項目は、スタートメニューにVRsettingの項目を追加してゲーム中に切り替えられる
# ようにできる？ ... まとめるのは上のメニューの件を対応してからにして ... 言語選択で日本語とそれ以外（英語）で表示言語別けてね"):
# "VR setting" in the Start menu (src/vr_menu.h, vr_menu_items.h, menu_assets.rc; layouts made by tools/make_vr_menu.py
# from Root.cpk into assets/ui, never published; [VR] PauseMenuVrSetting).
# 3.2.9 (the user, after the hardware run: "ベルトOK v001上げてアップして 修正 フェンサーの給弾ベルトが背中に固定されて
# いなかった 文章の確認は不要。そのままアップして"; +0.0.1): a Fencer gatling's feed belt. Carried with the gun's root, its
# drum and loop swung up into view in a reload; now the drum hangs on the player's back (the eye and the body's yaw) and
# the loop's two links ride with the gun where the game's own aim poses keep them for the gun's pitch (src/fencer_mount.h
# FencerBeltDrum, FencerBeltLink; fencer_dual.h FencerPlaceBelt). No setting, no INI key.
# 3.2.8 (the user, after the hardware run: "テスト完了 それじゃあまとめてv001上げてアップして 修正 バルガとデプスのヘッドボブを
# 少し軽減"; +0.0.1): the Barga's and the Depth Crawler's walking heave eased -- the cabin's height over the machine's
# own origin followed with a lag, its angles untouched (src/camera_math.h SteadyHeave, plugin.cpp SteadyWalkerHeave;
# [VR] BargaBobSmoothSeconds 4.0, DepthBobSmoothSeconds 0.35). Riding along, not in the notes: a hand launcher's
# missile (the Arm Hound) turned onto the hand's aim (src/shot_origin.h; not run on hardware), and [Diagnostics]
# MissionRequestPoints accepted beside EDF6MultiSlot 1.6.9's hook at 693F18 (src/diagnostic_points.cpp).
# 3.2.7 (the user, after the hardware runs: "ハウンドに大きな問題がなければアップ用にまとめて v001アップ 各種ミサイルの
# ロックオンサイトが出ていなかったバグを修正"; +0.0.1): the lock-on sight. Its frame is drawn on foot (it was drawn in
# vehicle seats only, src/aim_hud.cpp); every VR hand's weapon locks along that hand's own aim -- a Fencer's shoulder
# weapons too, and a Ranger's left gun, whose sight goes in the camera's second sight slot (src/lock_sight.h, new);
# the frame's lines are [Render] LockSightWidth of their first width, 0.3 by default (the user's choice).
# 3.2.6 (the user, after the hardware runs: "確認しました。v005上げて簡潔なアプデ内容を書いて 文章確認するからアップはまだ
# しないで"; +0.0.5): Fencer fixes only -- blades and the katana strike along the hand's aim (the muzzle carried onto
# the swing as it is at the weapon's tick, src/fencer_mount.h FencerSwingRows; the wave and the thrust turned onto
# the aim, src/shot_origin.h), a left weapon's lunge goes the left hand's way (the root motion taken at 5758C0,
# FencerTurnLunge), the Arm Hound on the forearm and the armsFlip / armsLowerFlip weapons turned over in the left
# hand (FencerMountLay). No setting, no INI key.
# 3.2.1 (the user, after an A/B on hardware: "見た目は問題なかったので規定オンにして まとめてv001上げてアップして ・修正 FPSを
# １０～２０％向上。試験機能。見た目に問題が出た場合、VR設定でオフにできます。"): [Render] OcclusionLeafOnly on by default
# (src/umbra_properties.h), its row in the in-game menu and in the settings exe; the THREADCPU report (src/thread_cpu.h).
# Named 3.2.0, not 3.1.13 (the user: "v3.2で。"), with three Japanese row names of the menu reworded, the line under
# its rows on a row's own background, and [VR] ShotBuzz 1 as the default ("射撃振動１を規定にして"); "修正したらこちらへの
# 確認無しでアップして".
# EDF6MultiSlot 1.6.9 as in 3.1.10.
# VERIFIED_* below: the 3.1.13 build (the hardware-tested 87E3179A code plus the 37-degree down cone and version
# strings).
VERIFIED_VR_SHA = '28F49D09A27E2AD1D1BE729B78F021462364FC7C6EC655B5D6C6217360F46A27'
VERIFIED_SETTINGS_SHA = '7DE1ACF972A5D3BBE97D56F85D35B80E665F51F0BFF7D983CB27CD2566DE484E'

def sha(data):
    return hashlib.sha256(data).hexdigest().upper()

def build(test_clear_loot=False):
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.read(ROOT/'packaging/EDF6VR.ini', encoding='utf-8-sig')
    source = ROOT/'packaging'
    for section, key, value in [
        ('VR','AutoStart','1'), ('VR','WeaponFollowsHand','1'),
        ('VR','ShotFromMuzzle','1'),('VR','VehicleCockpit','1'),('Render','HeadsetDisplay','1'),
        ('Render','NativeWorldStereo','1'),('Diagnostics','DepthProbe','1'),
        ('Diagnostics','LogGamepad','1')
    ]:
        assert cfg[section][key] == value, (section,key)
    for section,key in [('Test','AutoDeploy'),('Diagnostics','MissionRequestPoints'),
        ('Diagnostics','FiringPresentationTrace'),('Diagnostics','MotionTrace'),
        ('Render','SceneAA'),('Render','GpuProfile'),('Render','WarpABTest'),('Diagnostics','DevKeys')]:
        assert cfg[section][key] == '0', (section,key)
    assert cfg['Test']['PoseScript'] == '' and cfg['VR']['WatchBone'] == ''
    # 2.0 replaces an older INI once (ResetOlderIni); the file must say which generation it is.
    assert cfg['Settings']['Revision'] == '2', 'The shipped INI must carry [Settings] Revision=2'
    dll = (ROOT/'dist/EDF6VR.dll').read_bytes()
    assert f'EDF6VR {VERSION} cockpit loading'.encode() in dll, 'Build current DLL first'
    assert sha(dll) == VERIFIED_VR_SHA, 'Package the pinned, tested DLL'
    settings = (ROOT/'dist/EDF6 VR setting.exe').read_bytes()
    assert VERSION.encode('utf-16-le') in settings, 'Build the current settings program first'
    assert sha(settings) == VERIFIED_SETTINGS_SHA, 'Package the pinned, tested settings program'
    # 1.2.0: the positional code IS the audio fix, so it ships. What must not
    # ship is the research logging that shares the file, and the placement has
    # to be on -- a release with the file compiled in and the fix off would look
    # identical and sound like 1.1.0.
    assert b'SOUNDPOS' in dll, 'The positional sound fix must be built in'
    assert cfg['Diagnostics']['SoundPositionFix'] == '1', 'The audio fix must ship enabled'
    assert cfg['Diagnostics']['SoundPlaceVoices'] == '1'
    assert cfg['Diagnostics']['SoundPositionTrace'] == '0', 'Research logging must not ship on'
    assert cfg['Render']['MatchHeadsetFov'] == '1', 'The headset fit must ship enabled'
    assert cfg['Render']['ForceWidth'] == '3840' and cfg['Render']['ForceHeight'] == '2160',         'The fit falls back to these, so they stay at the wide default'
    for marker in (b'FENCERAUDIO',):
        assert marker not in dll or cfg['Diagnostics']['SoundPositionTrace'] == '0', marker.decode()
    assert cfg['Render']['RemoveFpsLimit']=='1', 'Failed cap trial must not become a release default'
    assert f'VERSION {VERSION} ' in (ROOT/'CMakeLists.txt').read_text(encoding='utf-8-sig')
    source = ROOT/'packaging'
    loot = ROOT.parent/'EDF6ClearLoot'
    loot_dll = (loot/'dist/EDF6ClearLoot.dll').read_bytes()
    assert b'EDF6ClearLoot 0.1.3 loading' in loot_dll, 'Build ClearLoot first'
    files = {
        'winmm.dll': (GAME/'winmm.dll').read_bytes(),
        'Mods/Plugins/EDF6VR.dll': dll,
        # Not Mods/Plugins/EDF6VR.ini: extracting an update must not replace the
        # player's settings. The DLL carries this file and writes it (or only its
        # missing keys) on start; the copy here is for reading.
        'EDF6VR/EDF6VR.defaults.ini': (source/'EDF6VR.ini').read_bytes(),
        'Mods/Plugins/EDF6ClearLoot.dll': loot_dll,
        'Mods/Plugins/EDF6ClearLoot.ini': (loot/'EDF6ClearLoot.ini').read_bytes(),
        'README_ClearLoot.txt': (source/'README_ClearLoot.txt').read_bytes(),
        'README_EDF6VR.txt': (source/'README_EDF6VR.txt').read_bytes(),
        # One window for what VR_Play.bat, Set_Resolution.bat and HD_Texture_2x.bat
        # did (it deletes those when it first opens), and the out-of-game settings.
        'EDF6 VR setting.exe': settings,
        # Updates an installed package to the latest GitHub release (tests/package_update_tests.ps1).
        'Update_EDF6VR.bat': (source/'Update_EDF6VR.bat').read_bytes(),
        'EDF6VR/Update-EDF6VR.ps1': (source/'Update-EDF6VR.ps1').read_bytes(),
        'EDF6VR/THIRD_PARTY_NOTICES.txt': (source/'THIRD_PARTY_NOTICES.txt').read_bytes(),
    }
    # A friends' test build may go out before its notes are written (1.7.9).
    notes = source/f'RELEASE_NOTES_{VERSION}.md'
    if notes.is_file():
        files[f'EDF6VR/RELEASE_NOTES_{VERSION}.md'] = notes.read_bytes()
    # EDFModLoader's own winmm, with the MultiSlot side's 1.5.5 thread-safety
    # repair: its 180 forwarders shared one call variable, so two threads could
    # send one another into the wrong Windows function. Reviewed and taken on
    # that side's request; the previous stock loader hashed B80E4DA6AE72...
    assert sha(files['winmm.dll']) == 'BE94E1FAC0CA12C41B6924E2EB168851641C999CE951D2A5A9FAEA5161B0F9A3', 'Unrecognized mod loader; review before redistribution'
    # EDF6MultiSlot, as released: the DLL and its README. Its winmm.dll must be
    # the very loader shipped here; its INI is not shipped (it writes defaults).
    multislot_bytes = MULTISLOT_ZIP.read_bytes()
    assert sha(multislot_bytes) == MULTISLOT_SHA, 'EDF6MultiSlot release ZIP changed; re-verify before bundling'
    with zipfile.ZipFile(MULTISLOT_ZIP) as ms:
        assert sha(ms.read('winmm.dll')) == sha(files['winmm.dll']), 'EDF6MultiSlot ships a different loader'
        files['Mods/Plugins/EDF6MultiSlot.dll'] = ms.read('Mods/Plugins/EDF6MultiSlot.dll')
        files['README_EDF6MultiSlot.txt'] = ms.read('README_EDF6MultiSlot.txt')
        files['HANDSHAKE_RECOVERY_JA.md'] = ms.read('HANDSHAKE_RECOVERY_JA.md')
        assert sha(files['Mods/Plugins/EDF6MultiSlot.dll']) == MULTISLOT_DLL_SHA
        assert not any(n.lower().endswith('.ini') for n in ms.namelist())
    assert b'EDF6MultiSlot' in files['Mods/Plugins/EDF6MultiSlot.dll']
    # The two settings tools must not build a pack or log paths of their own
    # accord: one is a one-line INI edit and the other only runs when the player
    # answers y.
    assert cfg['Diagnostics']['ResourcePathLog'] == '0', 'Path logging must not ship on'
    assert cfg['VR']['DashFollowsHead'] == '1'
    # No ModLoader.ini. Extracting one overwrote the player's own, which is how
    # 1.0.0-1.3.2 turned off ASI loading for their other mods and, with
    # Redirect=False, kept the HD textures from ever loading. Without the file
    # the loader runs on its defaults (plugins, ASI and Redirect all on), and
    # HD_Texture_2x.bat turns Redirect on in a player's own file if needed.
    assert 'ModLoader.ini' not in files
    assert 'Mods/Plugins/EDF6VR.ini' not in files, 'An update must not overwrite the player INI'
    assert (source/'EDF6VR.ini').read_bytes() in dll, 'The DLL must carry the current shipped INI; rebuild'
    assert (ROOT/'tools/edf6/loader_config.py').is_file()
    # The HD texture builder, staged fresh so the scripts in the archive are the
    # ones in the tree rather than whatever was last deployed.
    stage = stage_hdtexture.build()
    staged = 0
    for path in sorted(Path(stage).rglob('*')):
        if path.is_file():
            files['Mods/HDTexture/'+path.relative_to(stage).as_posix()] = path.read_bytes()
            staged += 1
    assert staged >= 50, staged
    assert 'Mods/HDTexture/loader_config.py' in files
    # The two redistributed binaries, pinned to the releases
    # tools/fetch_upscale_tools.py downloads. Anything else must not be shipped
    # under this project's name.
    for name, digest in (
        ('Mods/HDTexture/texconv.exe',
         'DCFDEC10244E02CF5037FBA089C55FB7E1326B1C8181742D77D15FA5CB5EEF06'),
        ('Mods/HDTexture/waifu2x/waifu2x-ncnn-vulkan.exe',
         '7EF2EFC4C54E1A963046B3A9B0AEE6BCA5B1487A7C907373035A10F4187A733D')):
        assert sha(files[name]) == digest, 'unexpected build of ' + name
    for p in sorted((source/'Licenses').glob('*.txt')):
        files['EDF6VR/Licenses/'+p.name] = p.read_bytes()
    assert len([n for n in files if '/Licenses/' in n]) >= 10
    # Windows batch text must use CRLF. This also makes all shipped text easy to
    # open in Windows editors, with no build-machine path substitutions. Only
    # text is touched: the bundled tools, the Python runtime and the upscaler
    # models are binaries and rewriting their line endings would destroy them.
    TEXT = ('.txt','.md','.ini','.bat','.cmd','.ps1','.json','.py','._pth')
    for name,data in files.items():
        # EDF6MultiSlot's README is shipped byte for byte (its own encoding).
        if name in ('README_EDF6MultiSlot.txt','HANDSHAKE_RECOVERY_JA.md'): continue
        if name.endswith(TEXT):
            text=data.decode('utf-8-sig').replace('\r\n','\n')
            files[name]=text.replace('\n','\r\n').encode('utf-8')
    git=['git','-c',f'safe.directory={ROOT.as_posix()}']
    commit=subprocess.check_output(git+['rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(git+['status','--porcelain'],cwd=ROOT,text=True).strip())
    loot_git=['git','-c',f'safe.directory={loot.as_posix()}']
    loot_commit=subprocess.check_output(loot_git+['rev-parse','HEAD'],cwd=loot,text=True).strip()
    loot_dirty=bool(subprocess.check_output(loot_git+['status','--porcelain'],cwd=loot,text=True).strip())
    manifest={'version':VERSION,'packageRevision':2,'sourceCommit':commit,'sourceDirty':dirty,
        'multiSlotZip':MULTISLOT_ZIP.name,'multiSlotZipSha256':MULTISLOT_SHA,
        'multiSlotRecoveryInGameVerified':False,'cockpitBoardingInGameVerified':True,
        'proteusGunnerInGameVerified':True,'proteusTandemCabinInGameVerified':False,
        'privateBuild':False,
        'clearLootSourceCommit':loot_commit,'clearLootSourceDirty':loot_dirty,'clearLootInGameVerified':True,
        'testedGameBuild':'17055427','files':{n:sha(d) for n,d in files.items()}}
    files['EDF6VR/PACKAGE_MANIFEST.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    out=ROOT/'release';out.mkdir(exist_ok=True)
    suffix='-ClearLoot-Test' if test_clear_loot else ''
    archive=out/f'EDF6VR-{VERSION}{suffix}.zip'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in sorted(files.items()):
            assert not name.startswith('/') and '..' not in Path(name).parts
            entry=zipfile.ZipInfo(name,(2026,9,20,0,0,0));entry.compress_type=zipfile.ZIP_DEFLATED
            z.writestr(entry,data)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert set(z.namelist()) == set(files)
        for n,d in files.items(): assert z.read(n)==d,n
        assert 'EDF6.exe' not in z.namelist() and 'EDF.dll' not in z.namelist()
    checksum=sha(archive.read_bytes())
    archive.with_suffix('.zip.sha256').write_text(f'{checksum}  {archive.name}\n',encoding='ascii')
    print(json.dumps({'archive':str(archive),'bytes':archive.stat().st_size,
        'sha256':checksum,'files':len(files),'sourceCommit':commit,'sourceDirty':dirty},indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--test-clear-loot',action='store_true',help='Keep the verified release ZIP unchanged')
    build(parser.parse_args().test_clear_loot)
