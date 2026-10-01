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
VERSION = '3.0.2'
# The online co-op mod built in _MultislotDEV, taken from its own release ZIP
# unchanged (its own package.ps1 pins the same loader hash).
MULTISLOT_ZIP = GAME/'_MultislotDEV/release/EDF6MultiSlot-1.5.33.zip'
MULTISLOT_SHA = 'A3B92B7D390514BE1289D1F831B4186A24EE2775A4DEEDF0C6E56CBEA30644A1'
MULTISLOT_DLL_SHA = '3C6D53BF111A031DE888836D6EE92B191AECAABD5B54FD7D5995C3EFFC4231AF'
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
VERIFIED_VR_SHA = '768D7A2247799942464EBA2CB95F974244B88B4F809A757E6AABAFB992C28CE5'
VERIFIED_SETTINGS_SHA = '9E5531F600129C6FEB3786DB3E269B0AAAF71DDB9EDB13FC95FCB759228C164F'

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
    assert b'EDF6ClearLoot 0.1.2 loading' in loot_dll, 'Build ClearLoot first'
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
