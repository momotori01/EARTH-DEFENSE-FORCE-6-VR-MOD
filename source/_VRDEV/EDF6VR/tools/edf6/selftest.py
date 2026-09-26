"""Prove the archive reader against the game's own files.

Nothing here is a mock. Every case takes a real archive out of the install,
takes it apart, puts it back together and demands the same bytes. A format that
round-trips shipped files is understood; one that only parses might be guessing,
and a texture pack built on a guess crashes a mission rather than looking
slightly wrong.

    python tools/edf6/selftest.py [game directory] [how many archives]
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpk as cpk_module
import rab as rab_module
import hd_textures as hd_module
import loader_config as loader_module


def game_directory(argument=None):
    if argument:
        return argument
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, '..', '..', '..', '..'))


def check_archive(blob, label):
    archive = rab_module.parse(blob)
    rebuilt = archive.to_bytes()
    if rebuilt != blob:
        where = next((i for i in range(min(len(rebuilt), len(blob))) if rebuilt[i] != blob[i]),
                     min(len(rebuilt), len(blob)))
        raise AssertionError('%s: rebuilt %d bytes vs %d, first difference at %d'
                             % (label, len(rebuilt), len(blob), where))
    # Decompression is checked separately from the container: a block the
    # archive says is CMPL must come back at exactly the length its own header
    # promises, which is the only way a wrong LZSS decode stays quiet.
    for entry in archive.entries:
        if entry.stored[:4] == b'CMPL':
            promised = struct.unpack_from('>I', entry.stored, 4)[0]
            # entry.data is what forces the decompression; reading it here is
            # the point, since nothing else in a real run would.
            if len(entry.data) != promised:
                raise AssertionError('%s/%s: %d bytes decompressed, header says %d'
                                     % (label, entry.name, len(entry.data), promised))
    return archive


def check_rewrite(blob, label):
    """The path a texture pack actually takes: replace a block, write, read back.

    Rewriting one entry with its own contents has to move every following block
    (the stored form is written afresh and is larger), so this exercises the
    offsets, the two size fields and the name tables all at once.
    """
    archive = rab_module.parse(blob)
    target = next((e for e in archive.entries if e.name.upper().endswith('.DDS')), None)
    if target is None:
        return 0
    original = target.data
    target.replace(original)
    rewritten = archive.to_bytes()
    again = rab_module.parse(rewritten)
    if len(again.entries) != len(archive.entries):
        raise AssertionError('%s: %d entries became %d' % (label, len(archive.entries),
                                                           len(again.entries)))
    for before, after in zip(archive.entries, again.entries):
        if (before.name, before.directory, before.flags) != (after.name, after.directory,
                                                             after.flags):
            raise AssertionError('%s: record changed for %s' % (label, before.name))
        if before.data != after.data:
            raise AssertionError('%s: contents of %s did not survive' % (label, before.name))
    largest_stored, largest_plain = struct.unpack_from('<II', rewritten, 0x0C)
    if largest_stored != max(len(e.stored) for e in again.entries):
        raise AssertionError('%s: largest stored block is wrong' % label)
    if largest_plain != max(len(e.data) for e in again.entries):
        raise AssertionError('%s: largest decompressed block is wrong' % label)
    return len(rewritten) - len(blob)


def check_batching():
    """No command line handed to a tool may grow past what Windows accepts.

    A city map has five hundred textures with ninety-character paths, which is
    fifty thousand characters on one line; the limit is about thirty-two
    thousand. Whether a build crossed it used to depend on how much of the map
    was already cached, so it worked one day and stopped the next.
    """
    class Recording(hd_module.Tools):
        def __init__(self):
            self.calls = []

        def run(self, arguments):
            self.calls.append(arguments)

    deep = 'S:/SteamLibrary/steamapps/common/EARTH DEFENSE FORCE 6/Mods/HDTextureWork/in/t%05d.dds'
    files = [deep % i for i in range(900)]
    tools = Recording()
    tools.run_over(['texconv.exe', '-nologo', '-ft', 'png'], files, 'S:/out')
    longest = max(sum(len(a) + 3 for a in call) for call in tools.calls)
    if longest >= 32000:
        raise AssertionError('a command line of %d characters was built' % longest)
    passed = [a for call in tools.calls for a in call if a.endswith('.dds')]
    if passed != files:
        raise AssertionError('batching lost or reordered files: %d of %d'
                             % (len(passed), len(files)))
    empty = Recording()
    empty.run_over(['texconv.exe'], [], 'S:/out')
    if empty.calls:
        raise AssertionError('an empty list should run nothing')
    one = Recording()
    one.run_over(['texconv.exe'], ['a.dds'], 'S:/out')
    if len(one.calls) != 1:
        raise AssertionError('one file should be one call')
    print('command lines: %d files split into %d calls, longest %d characters'
          % (len(files), len(tools.calls), longest))


def check_naming():
    """The colour/property decision, on names taken out of the shipped maps.

    Each of these was settled by decoding the texture and looking at it. They
    are here because the rule is the only part of the pack that cannot be
    checked by rebuilding a file: a normal map that is called a picture still
    produces a valid archive, it just puts invented detail into data a shader
    reads as numbers.
    """
    colour = (
        'sk_noren01_NT',                    # a shop curtain with writing on it
        'nw_fld_HanyouDirtDF1_NT',          # painted ground; _NT sits after _DF
        'nw_fld_brankstonedf01_nt',
        'sk_Bank01', 'sk_bd1010_A01_lod', 'sk_closeshop01_lod_nt',
        'sk_barber01_sign1-df', 'sk_BD3020_1F01_LOD_FAL', 'sk_tile001',
        'com_nw_largevisionad_fal01',       # a billboard advert
        'ya_edf6_room_box_light_col', 'maser501_df01', 'yy_dagasiya01_nt',
    )
    surface = (
        'nw_fld_brankstonenm01_nt',         # a normal map whose name ends _nt
        'nw_bld_shoptent2fmk01_nt',
        'ym_loadsignmontyu01_ao_nt',
        'ym_newsignal01_para_nt',
        'ym_signallight01_nom_nt',
        'com_nw_largevisionad_fsw01',
        'com_nw_brankstone_fpr02wide',
        's_assault_af14_rgb',               # three property channels, not colour
        's_assault_af14_nm', 'sk_Bank01_light_FPR', 'sk_dirts1010-1f01_ms',
        'ya_edf6_room_box_light_nor', 'v510_maser_rmol01', 'maser501_rgba01',
        'sk_bd3030_1f02_ao', 'fld_nw_gakecon01_mk01', 'window_normal_noise',
        'com_nw_wall_r107g117b119',
    )
    wrong = []
    for name in colour:
        if hd_module.is_property(name.lower()):
            wrong.append('%s should be colour, marker %r'
                         % (name, hd_module.marker(name.lower())))
    for name in surface:
        if not hd_module.is_property(name.lower()):
            wrong.append('%s should be a surface map, marker %r'
                         % (name, hd_module.marker(name.lower())))
    if wrong:
        raise AssertionError('naming rule: ' + '; '.join(wrong))
    print('naming rule: %d colour and %d surface names classified correctly'
          % (len(colour), len(surface)))


def check_loader_config():
    import codecs
    import tempfile
    on, switch = loader_module.redirect_on, loader_module.switch_on
    cases = [
        ('', True),
        ('[Other]\nRedirect=False\n', True),              # not the loader's section
        ('[ModLoader]\nLoadASI=False\n', True),            # key missing: default on
        ('[ModLoader]\r\nRedirect=True\r\n', True),
        ('[modloader]\n  redirect =  TRUE \n', True),
        ('[ModLoader]\nRedirect=False\n', False),
        ('[ModLoader]\nRedirect=1\n', False),              # the loader only accepts True
        ('[ModLoader]\n;Redirect=False\nRedirect=False\n', False),
        ('[ModLoader]\nRedirect=False\n[X]\nRedirect=True\n', False),
    ]
    for text, expected in cases:
        assert on(text) == expected, text
        assert on(switch(text)), text
    before = ('; mine\r\n[ModLoader]\r\nLoadPlugins=True\r\nLoadASI=True\r\n'
              'Redirect=False\r\nGameLog=True\r\n')
    after = switch(before)
    assert after == before.replace('Redirect=False', 'Redirect=True'), after
    folder = tempfile.mkdtemp()
    assert loader_module.ensure_redirect(folder) == 'default'
    path = os.path.join(folder, 'ModLoader.ini')
    open(path, 'wb').write(codecs.BOM_UTF8 + before.encode('utf-8'))
    assert loader_module.ensure_redirect(folder) == 'switched'
    assert open(path, 'rb').read() == codecs.BOM_UTF8 + after.encode('utf-8')
    assert loader_module.ensure_redirect(folder) == 'on'
    open(path, 'wb').write(codecs.BOM_UTF16_LE + before.encode('utf-16-le'))
    assert loader_module.ensure_redirect(folder) == 'switched'
    assert open(path, 'rb').read() == codecs.BOM_UTF16_LE + after.encode('utf-16-le')
    print('loader config: %d readings and 3 rewrites match the loader' % len(cases))


def main():
    root = game_directory(sys.argv[1] if len(sys.argv) > 1 else None)
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 120
    archive = cpk_module.Cpk(os.path.join(root, 'Root.cpk'))
    print('Root.cpk: %d entries' % len(archive))

    wanted = list(archive.names(suffix='.RAB')) + list(archive.names(suffix='.MRAB'))
    # Smallest first, then a few of the big ones, so a break shows up in a
    # second rather than after half a gigabyte of decompression.
    wanted.sort(key=lambda n: int(archive.index[n]['ExtractSize']))
    sample = wanted[:limit] + wanted[-3:]

    started = time.time()
    checked = total = textures = models = 0
    grew = 0
    for directory, name in sample:
        blob = archive.read(directory, name)
        parsed = check_archive(blob, '%s/%s' % (directory, name))
        checked += 1
        total += len(blob)
        textures += sum(1 for e in parsed.entries if e.directory.endswith('TEXTURE'))
        models += sum(1 for e in parsed.entries if e.directory == 'MODEL')
        if len(blob) < 40 * 1024 * 1024:
            grew += check_rewrite(blob, '%s/%s' % (directory, name))
    print('round tripped %d archives, %.1f MB, %d textures, %d models, in %.1fs'
          % (checked, total / 1e6, textures, models, time.time() - started))
    print('rewriting one texture per archive cost %.1f kB of stored-mode overhead' % (grew / 1e3))

    check_naming()
    check_batching()
    check_loader_config()

    # The stored-mode writer has to survive its own decoder, because that is the
    # exact stream the game will be handed for anything we change.
    for case in (b'', b'a', b'12345678', os.urandom(70000), bytes(50000)):
        if rab_module.unwrap(rab_module.wrap(case)) != case:
            raise AssertionError('stored CMPL round trip failed at %d bytes' % len(case))
    print('stored CMPL round trip: ok')
    print('PASS')


if __name__ == '__main__':
    main()
