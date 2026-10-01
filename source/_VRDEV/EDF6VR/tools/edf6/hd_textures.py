"""Build the HD texture pack from the user's own EDF6 install.

Nothing of the game's is shipped and nothing of the game's is written. The
archives are opened read-only, the textures worth enlarging are taken out,
doubled, put back into a rebuilt copy of the archive, and the copy is written
under Mods/, where EDFModLoader's own redirector picks it up. Deleting that
folder puts everything back.

    python hd_textures.py --game <EDF6 directory> [--set all] [--only NAME]

Which textures are chosen, and why
----------------------------------
An archive holds the same texture twice: full size under HD-TEXTURE, and a
sixteenth-width copy under TEXTURE for the game's low texture setting. Only the
full-size one is touched; the small copies are what the low setting is for.

Of those, only colour maps are enlarged. The format cannot be used to tell them
apart -- in this game a normal map and a building facade are both BC1 -- so the
name is what decides, and the naming is consistent across the maps: a trio of
<thing>df / <thing>nm / <thing>mk for colour, normal and mask, with _FAL, _COL,
_ALBEDO and unmarked names also being colour, and _FPR, _MS, _AO, _RMO, _RGB,
_PARA, _FSW and the rest being surface properties. Every one of those was
settled by decoding examples and looking at them rather than by reading the
letters, and two of them are the opposite of what the name suggests: _RGB is not
a colour image, it is three property channels packed into one picture, and _NT
is not a normal map.

_NT is worse than misleading, it is not a type at all. It is a modifier that
sits after the type: nw_fld_brankstonedf01_nt is stone colour and
nw_fld_brankstonenm01_nt is that stone's normal map. Reading only the last word
of the name calls both of them pictures, which is why the word is found by
peeling the modifiers off first and judging what is underneath. The same goes
for _LOD and the glued "wide".

Enlarging a property map is not a smaller benefit, it is wrong: those channels
are numbers the shader reads, and an upscaler invents plausible pictures.

Size bounds exist for the same kind of reason. Below 128 pixels a texture is a
tiling detail or a flat swatch, where an upscaler adds invented grain to
something that was never meant to be looked at closely. Above 2048 the texture
already out-resolves anything the player can stand next to.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cpk as cpk_module
import rab as rab_module
import dds as dds_module
import progress as progress_module
import gpu as gpu_module
import check as check_module
import loader_config as loader_module

# Trailing markers that mean the texture is a surface property rather than a
# picture. The glued list is matched at the end of the name because the game
# writes "concretenm" as often as "concrete_nm"; the token list is matched only
# as a whole word, because two letters at the end of an ordinary word would
# otherwise throw away colour maps.
GLUED_PROPERTY = ('nm', 'nom', 'nor', 'mk', 'msk', 'fpr', 'fnm', 'rmol', 'rmo', 'rgb',
                  'rgba', 'ao')
# "rso" joined on 2026-09-26: the Air Raider's radio (musenki-rso) had been
# enlarged as a picture.
TOKEN_PROPERTY = frozenset(('n', 'no', 'normal', 'ms', 'mask', 'rmx', 'rss', 'rsso', 'rso', 'fao',
                            'hk', 'plm', 'cco', 'dist', 'para', 'fsw', 'sw', 'alpha',
                            'noise', 'dummy', 'spec', 'sp', 'orm', 'prm', 'em', 'emi', 'ym',
                            'height', 'disp', 'bump', 'gloss', 'cavity'))
# Words that sit after the type marker instead of replacing it. "_NT" is the
# trap: nw_fld_brankstonedf01_nt is stone colour and nw_fld_brankstonenm01_nt is
# its normal map, and both end in _nt, so reading only the last word calls a
# normal map a picture. Peel these off and judge what is underneath.
MODIFIER = frozenset(('nt', 'lod', 'wide'))
# Flat colour swatches, named for the colour they hold: com_nw_wall_r107g117b119.
# The last group of digits is gone by the time this is tested, because every
# marker has its trailing number stripped, so the blue channel may be empty.
SWATCH = re.compile(r'^r\d+g\d+b\d*$')
TRAILING_DIGITS = re.compile(r'\d+$')

SMALLEST = 128      # below this a texture is detail noise or a flat colour
# The ceiling is 2048 rather than 1024 because of where the 2048 textures
# actually are: ground, concrete, asphalt, dead grass and wall stone, tiled
# across a whole map. In VR the ground is a metre and a half from the eye and
# fills much of the view, so that tier is the one worth spending on. It costs
# about 300 MB of video memory on a mission, measured against the paths one
# session really loaded, and the heaviest city map lands at 1.75 GB of texture.
LARGEST = 2048

# City and town maps. Everything is built by default; this list only serves
# --set city, for working on the streets without waiting for the mountains.
#
# The mountains, caves, coast and the base between missions are not a lesser
# case: the base is where the longest unhurried look at anything happens, and a
# cave wall is as close to the eye as a shop front.
CITY_MAPS = (
    'IG_2000MCITY', 'IG_2000MCITY601', 'NW_KOUSOUBLD601', 'NW_DANTI601', 'NW_DANTI01',
    'NW_DANTI01_HAIKYO', 'NW_KOKYUJYUTAKU01', 'NW_TRAINCITY', 'NW_JYOUSUICITY',
    'NW_KITAGUNICITY', 'NW_SEIYU', 'NW_SEIYU_NIGHT', 'NW_HAIKYO601', 'NW_HAIKYO602',
    'IG_HAIKYO603', 'NW_RINKAI01', 'NW_RINKAI02', 'NW_HENDEN', 'NW_HENDEN_WINTER',
    'NW_EUROPE01', 'NW_DLCMAP601', 'NW_DLCMAP602',
)


def selected(entry, info, has_hd):
    """True when this entry is a colour map worth doubling."""
    if not entry.name.upper().endswith('.DDS'):
        return False
    if has_hd and entry.directory == 'TEXTURE':
        return False                       # the game's own low-setting copy
    if entry.directory == 'MODEL':
        return False
    if not info.ok or not info.colour:
        return False
    if info.width < 4 or info.height < 4:
        return False
    longest = max(info.width, info.height)
    if longest < SMALLEST or longest > LARGEST:
        return False
    stem = entry.name.rsplit('.', 1)[0].lower()
    if stem.endswith('.lod'):
        return False
    return not is_property(stem)


def marker(stem):
    """The word that says what kind of texture this is.

    Names are written both ways round -- concrete_nm and concretenm -- and some
    carry a modifier after the marker, so the word is found by dropping the
    modifiers and any trailing number and reading what is left.
    """
    words = [w for w in re.split(r'[_-]', stem) if w]
    while len(words) > 1:
        last = TRAILING_DIGITS.sub('', words[-1]) or words[-1]
        if last in MODIFIER:
            words.pop()
            continue
        if last.endswith('wide'):
            words[-1] = last[:-4]
            continue
        break
    if not words:
        return ''
    word = TRAILING_DIGITS.sub('', words[-1]) or words[-1]
    return word[:-4] if word.endswith('wide') and len(word) > 4 else word


def is_property(stem):
    """True when the name says this holds surface data rather than a picture."""
    word = marker(stem)
    if not word:
        return False
    return bool(SWATCH.match(word) or word in TOKEN_PROPERTY
                or word.endswith(GLUED_PROPERTY))


class Tools:
    def __init__(self, root):
        self.texconv = os.path.join(root, 'texconv.exe')
        self.waifu2x = os.path.join(root, 'waifu2x', 'waifu2x-ncnn-vulkan.exe')
        self.model = os.path.join(root, 'waifu2x', 'models-upconv_7_photo')
        for path in (self.texconv, self.waifu2x):
            if not os.path.isfile(path):
                raise SystemExit('missing tool: %s' % path)

    def run(self, arguments):
        done = subprocess.run(arguments, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if done.returncode != 0:
            raise RuntimeError('%s failed (%d):\n%s' % (os.path.basename(arguments[0]),
                                                        done.returncode,
                                                        done.stdout.decode('utf-8', 'replace')))

    def run_over(self, prefix, files, target_directory):
        """`prefix` applied to `files`, split so no command line grows too long.

        Windows refuses a command line past about 32,000 characters, and a city
        map has five hundred textures whose paths are ninety characters each.
        Whether that limit was reached used to depend on how much of the map was
        already in the cache, so the same build would work one day and stop with
        "the filename or extension is too long" the next.
        """
        room = 24000 - sum(len(argument) + 3 for argument in prefix) - len(target_directory)
        at = 0
        while at < len(files):
            batch, used = [], 0
            while at < len(files) and (not batch or used < room):
                used += len(files[at]) + 3
                batch.append(files[at])
                at += 1
            self.run(prefix + ['-o', target_directory, '-y'] + batch)

    def to_png(self, source_directory, target_directory):
        files = [os.path.join(source_directory, n) for n in os.listdir(source_directory)]
        if files:
            self.run_over([self.texconv, '-nologo', '--ignore-srgb', '-ft', 'png'],
                          files, target_directory)

    def double(self, source_directory, target_directory, gpu=None):
        arguments = [self.waifu2x, '-i', source_directory, '-o', target_directory,
                     '-s', '2', '-n', '0', '-m', self.model, '-f', 'png']
        if gpu is not None:
            arguments += ['-g', str(gpu)]
        self.run(arguments)

    def to_dds(self, files, target_directory, info):
        self.run_over([self.texconv, '-nologo', '--ignore-srgb']
                      + dds_module.texconv_arguments(info), files, target_directory)

    def to_thumbnails(self, files, target_directory):
        """Eight pixels square, for comparing a result against its source."""
        self.run_over([self.texconv, '-nologo', '--ignore-srgb', '-ft', 'bmp',
                       '-f', 'B8G8R8X8_UNORM', '-w', '8', '-h', '8'],
                      files, target_directory)


def fresh(path):
    if os.path.isdir(path):
        shutil.rmtree(path)
    os.makedirs(path)
    return path


def enlarge(tools, names, work, gpu, run, first, last):
    """Read, enlarge, save and check one batch. Returns (kept stems, lost stems).

    The results are left in work/out under their stem names; the caller decides
    where they go.
    """
    staged = fresh(os.path.join(work, 'in'))
    pngs = fresh(os.path.join(work, 'png'))
    doubled = fresh(os.path.join(work, 'x2'))
    encoded = fresh(os.path.join(work, 'out'))
    for stem, (entry, _info, _key) in names.items():
        open(os.path.join(staged, stem + '.dds'), 'wb').write(entry.data)

    # Each step writes its results into a folder of its own, so counting that
    # folder says how far it has got without the tools having to report
    # anything. Enlarging takes far longer than the two conversions, so it gets
    # most of the bar.
    def at(share):
        return first + (last - first) * share

    with run.step('reading', pngs, len(names), at(0.00), at(0.15)):
        tools.to_png(staged, pngs)
    with run.step('enlarging', doubled, len(names), at(0.15), at(0.85)):
        tools.double(pngs, doubled, gpu)
    # Back to the exact format each one came in, one texconv run per format.
    groups = {}
    for stem, (_entry, info, _key) in names.items():
        groups.setdefault((info.format, info.dx10), []).append(stem)
    with run.step('saving', encoded, len(names), at(0.85), at(1.0)):
        for (_format, _dx10), stems in groups.items():
            _entry, info, _key = names[stems[0]]
            tools.to_dds([os.path.join(doubled, s + '.png') for s in stems], encoded, info)
    # texconv writes .DDS on some builds and .dds on others; settle it here so
    # the picture check has one name to look for.
    for stem in names:
        produced = os.path.join(encoded, stem + '.DDS')
        if os.path.isfile(produced) and produced != os.path.join(encoded, stem + '.dds'):
            os.replace(produced, os.path.join(encoded, stem + '.dds'))
    return check_module.survivors(tools, staged, encoded, work, list(names))


def upscale_archive(blob, tools, work, cache, gpu=None, run=None):
    """A rebuilt archive, or None when nothing in it was worth doubling."""
    archive = rab_module.parse(blob)
    has_hd = any(d == 'HD-TEXTURE' for d in archive.directories)

    wanted = []
    for entry in archive.entries:
        info = dds_module.read(entry.head())
        if selected(entry, info, has_hd):
            wanted.append((entry, info))
    if not wanted:
        return None, 0, 0, 0

    # Textures repeat inside a map and across maps, so the pack is built once
    # per distinct picture and reused everywhere it appears.
    from_cache = 0
    refused = 0
    todo = []
    keys = {}
    for entry, info in wanted:
        key = hashlib.sha1(entry.data).hexdigest()
        keys[id(entry)] = key
        cached = os.path.join(cache, key + '.dds')
        if os.path.isfile(cached):
            from_cache += 1
        else:
            todo.append((entry, info, key))

    if todo:
        names = {}
        for index, (entry, info, key) in enumerate(todo):
            names['t%05d' % index] = (entry, info, key)
        def store(stems):
            # Before any retry: the next pass empties work/out to start clean,
            # and everything that already came out right is in there.
            for stem in stems:
                _entry, _info, key = names[stem]
                shutil.move(os.path.join(work, 'out', stem + '.dds'),
                            os.path.join(cache, key + '.dds'))

        kept, lost = enlarge(tools, names, work, gpu, run, 0.0, 0.97)
        store(kept)
        # A failure is usually the graphics driver having a bad moment rather
        # than anything about the picture: every one seen so far came out
        # correctly the second time. So the few that failed are tried once more
        # on their own, and only then given up on.
        if lost:
            again = {stem: names[stem] for stem in lost}
            recovered, lost = enlarge(tools, again, work, gpu, run, 0.97, 1.0)
            store(recovered)
        refused += len(lost)

    replaced = 0
    for entry, info in wanted:
        cached = os.path.join(cache, keys[id(entry)] + '.dds')
        if not os.path.isfile(cached):
            continue
        new = open(cached, 'rb').read()
        check = dds_module.read(new)
        if not check.ok or check.width != info.width * 2 or check.height != info.height * 2:
            continue                       # refuse anything that did not come back doubled
        entry.replace(new)
        replaced += 1
    if not replaced:
        return None, 0, from_cache, refused
    return archive.to_bytes(), replaced, from_cache, refused


# What the pack is built with. Revision 2 (2026-09-26): textures with an alpha
# channel keep their colour in every mip (dds.texconv_arguments) and RSO maps
# are left alone. Revision 1 let texconv weight the colour by the alpha while
# it made the mips, which darkened every mip after the first wherever the alpha
# is low, and it had enlarged the radio's RSO map besides. A pack made by
# revision 1 has the archives holding either made again, from the game's own
# files as always; the rest are kept.
#
# Revision 3 (2026-10-01): textures holding several surfaces -- an array or a
# cube map -- are left alone (dds.read). Revisions 1 and 2 enlarged their first
# surface and dropped the rest: the terrain colour of 23 maps is an array of 2
# to 19 slices, and the ground of mission 65 (IG_CAVE604_2, 8 slices) came out
# white; one enemy's nebula cube lost five faces. Those 24 archives are made
# again, the rest kept.
PACK_REVISION = 3


def source_textures(sources, directory, name):
    """The header facts of every texture in the game's own copy of an archive."""
    for source in sources:
        entry = source.index.get((directory, name))
        if entry is None or int(entry['ExtractSize']) != int(entry['FileSize']):
            continue
        start = source.base + int(entry['FileOffset'])
        return {(d, n): dds_module.read(head)
                for d, n, head in rab_module.heads(source.path, start=start)}
    return None


def made_wrong(path, original, revision=1):
    """True when a written archive holds a texture an earlier revision enlarged wrongly.

    Enlarged is told from the game's own copy: twice its width. Revision 1 got
    alpha maps and RSO maps wrong; revisions 1 and 2 got every texture of more
    than one surface wrong. Most archives also carry alpha maps that were never
    enlarged, and those are fine.
    """
    if not original:
        return False
    for directory, name, head in rab_module.heads(path):
        if directory != 'HD-TEXTURE' or not name.upper().endswith('.DDS'):
            continue
        game = original.get((directory, name))
        info = dds_module.read(head)
        if game is None or not game.width or info.width != 2 * game.width:
            continue
        if game.surfaces > 1:
            return True
        if revision < 2 and (info.format in dds_module.HAS_ALPHA
                             or marker(name.rsplit('.', 1)[0].lower()) == 'rso'):
            return True
    return False


def bring_up_to_date(root, work, cache, record, sources, say):
    """Remove what an earlier revision made wrong, so the run below makes it again."""
    stamp = os.path.join(work, 'revision.txt')
    try:
        revision = int(open(stamp, encoding='ascii').read().strip())
    except (OSError, ValueError):
        revision = 1
    if revision >= PACK_REVISION:
        return
    written = []
    if os.path.isfile(record):
        written = list(dict.fromkeys(line for line in open(record, encoding='utf-8').read().splitlines() if line))
    if written:
        say('Checking the files an earlier version made...')
    stale = []
    for line in written:
        path = os.path.join(root, line)
        parts = line.split('/')   # written.txt keeps '/' (see where it is appended)
        if len(parts) == 3 and os.path.isfile(path) and made_wrong(path, source_textures(sources, parts[1], parts[2]),
                                                                    revision):
            stale.append(line)
    if revision < 2 and os.path.isdir(cache):
        for name in os.listdir(cache):
            path = os.path.join(cache, name)
            with open(path, 'rb') as handle:
                head = handle.read(dds_module.DX10_HEADER)
            if dds_module.read(head).format in dds_module.HAS_ALPHA:
                os.remove(path)
    for line in stale:
        os.remove(os.path.join(root, line))
    if stale:
        if revision < 2:
            say('%d of them are made again: textures with transparency had lost their colour' % len(stale))
            say('when seen from further away, and some ground had turned white.')
        else:
            say('%d of them are made again: their ground had turned white.' % len(stale))
        say('The other %d are kept.' % (len(written) - len(stale)))
        say('')
    os.makedirs(work, exist_ok=True)
    open(stamp, 'w', encoding='ascii').write('%d\n' % PACK_REVISION)


def is_city(stem):
    """A city map, or one of its weather variants.

    The variants are separate archives of twenty to thirty-five megabytes
    carrying the sky and lighting for the same place, so NW_HAIKYO601_EVENING
    has to be built alongside NW_HAIKYO601 or half the missions on that map are
    left untouched.
    """
    return stem in CITY_MAPS or any(stem.startswith(name + '_') for name in CITY_MAPS)


def archives_for(set_name, archives):
    """(cpk, directory, name) for everything in the chosen set."""
    chosen = []
    for source in archives:
        for directory, name in source.names(suffix='.RAB'):
            stem = name.rsplit('.', 1)[0].upper()
            if directory == 'WEAPON' and set_name in ('weapons', 'all'):
                chosen.append((source, directory, name))
            elif directory == 'MAP' and (set_name == 'all' or
                                         (set_name == 'city' and is_city(stem))):
                chosen.append((source, directory, name))
        if set_name in ('weapons', 'all'):
            for directory, name in source.names(suffix='.MRAB'):
                if directory == 'OBJECT':
                    chosen.append((source, directory, name))
    return chosen


def find_game(start):
    path = start
    for _ in range(6):
        if os.path.isfile(os.path.join(path, 'EDF6.exe')):
            return path
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    return None


def find_tools(start):
    for candidate in (start, os.path.abspath(os.path.join(start, '..', 'upscale'))):
        if os.path.isfile(os.path.join(candidate, 'texconv.exe')):
            return candidate
    return start


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', default=None)
    # "all" is what the .bat builds: every map, every weapon, and the enemies,
    # vehicles and characters. The other two are for working on one part at a
    # time.
    parser.add_argument('--set', dest='set_name', default='all',
                        choices=('city', 'weapons', 'all'))
    parser.add_argument('--only', default=None, help='one archive name, for testing')
    parser.add_argument('--limit', type=int, default=0)
    parser.add_argument('--gpu', type=int, default=None)
    parser.add_argument('--tools', default=None)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--remove', action='store_true',
                        help='delete exactly the files a previous run wrote')
    options = parser.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    # The same file runs from the source tree and from Mods/HDTexture in an
    # install, so both roots are found by looking rather than by counting how
    # many times to go up.
    root = options.game or find_game(here)
    tools_root = options.tools or find_tools(here)
    if not root or not os.path.isfile(os.path.join(root, 'EDF6.exe')):
        raise SystemExit('Cannot find EARTH DEFENSE FORCE 6. Pass --game <folder>.')

    mods = os.path.join(root, 'Mods')
    work = os.path.join(mods, 'HDTextureWork')
    cache = os.path.join(work, 'cache')
    os.makedirs(cache, exist_ok=True)

    # Everything written is recorded, so turning the pack off removes what this
    # made and nothing else. Mods/MAP is shared with any other mod the user has
    # installed, so deleting the folder would not be safe.
    record = os.path.join(work, 'written.txt')
    # Written when a whole run gets to the end, so "EDF6 VR setting.exe" can tell
    # a finished pack from one that was stopped halfway. Beside this script
    # rather than in HDTextureWork, which players are told they may delete.
    finished = os.path.join(here, 'complete.txt')
    whole_run = not options.only and not options.limit and not options.dry_run
    if options.remove:
        if os.path.isfile(finished):
            os.remove(finished)
        removed = 0
        if os.path.isfile(record):
            for line in open(record, encoding='utf-8').read().splitlines():
                victim = os.path.join(root, line)
                if os.path.isfile(victim):
                    os.remove(victim)
                    removed += 1
        if os.path.isdir(work):
            shutil.rmtree(work, ignore_errors=True)
        for directory in ('MAP', 'WEAPON', 'OBJECT'):
            path = os.path.join(mods, directory)
            if os.path.isdir(path) and not os.listdir(path):
                os.rmdir(path)
        progress_module.say('HD textures removed: %d files' % removed)
        return

    # The pack is loose files, and only the loader's redirector reads them. A
    # player whose other mods came with Redirect=False would build forty
    # gigabytes the game never opens.
    if not options.dry_run:
        state = loader_module.ensure_redirect(root)
        if state == 'switched':
            progress_module.say('ModLoader.ini had Redirect=False. HD textures need it, so it is now')
            progress_module.say('Redirect=True. Nothing else in that file was changed.')

    tools = Tools(tools_root)

    sources = []
    for name in ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk'):
        path = os.path.join(root, name)
        if os.path.isfile(path):
            sources.append(cpk_module.Cpk(path))

    # A whole run brings an older pack up to date; --only and --limit are for
    # trying one archive, and must not take the rest of the pack away.
    if not options.only and not options.limit:
        bring_up_to_date(root, work, cache, record, sources, progress_module.say)
    chosen = archives_for(options.set_name, sources)
    if options.only:
        chosen = [c for c in chosen if c[2].upper() == options.only.upper()]
        if not chosen:
            raise SystemExit('no archive called %s' % options.only)
    if options.limit:
        chosen = chosen[:options.limit]

    say = progress_module.say
    if options.dry_run:
        say('%d files in set "%s"' % (len(chosen), options.set_name))
        for _source, directory, name in chosen:
            say('  %s/%s' % (directory, name))
        return

    sizes = [int(source.index[(directory, name)]['ExtractSize'])
             for source, directory, name in chosen]
    say('')
    say('EDF6 VR - HD Textures')
    total = sum(sizes)
    say('%d files to make, %s to read.'
        % (len(chosen), '%.0f GB' % (total / 1e9) if total >= 1e9 else '%.0f MB' % (total / 1e6)))
    say('You can leave this window open. Do not start the game yet.')
    say('')

    card, card_name = gpu_module.choose(tools, work, options.gpu)
    say('Using: %s' % card_name)
    say('')

    if whole_run and os.path.isfile(finished):
        os.remove(finished)
    run = progress_module.Run(sum(sizes), len(chosen))
    started = time.time()
    written = skipped = 0
    grew = refused_total = 0
    for index, ((source, directory, name), size) in enumerate(zip(chosen, sizes), 1):
        target = os.path.join(mods, directory, name)
        run.begin(size, name, index)
        if os.path.isfile(target):
            run.end('already made')
            skipped += 1
            continue
        blob = source.read(directory, name)
        rebuilt, replaced, cached, refused = upscale_archive(blob, tools, work, cache,
                                                             card, run)
        if rebuilt is None:
            run.end('nothing here needs it')
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        temporary = target + '.part'
        open(temporary, 'wb').write(rebuilt)
        os.replace(temporary, target)
        with open(record, 'a', encoding='utf-8') as note:
            print(os.path.relpath(target, root).replace(os.sep, '/'), file=note)
        grew += len(rebuilt) - len(blob)
        written += 1
        refused_total += refused
        run.end('%d pictures%s.  %.0f MB -> %.0f MB%s'
                % (replaced, ' (%d ready)' % cached if cached else '',
                   len(blob) / 1e6, len(rebuilt) / 1e6,
                   '.  %d left alone' % refused if refused else ''))

    run.complete()
    if whole_run:
        with open(finished, 'w', encoding='utf-8') as note:
            print('HD textures made %s: %d files made, %d already there.'
                  % (time.strftime('%Y-%m-%d %H:%M'), written, skipped), file=note)
    say('')
    say('All done. %d files made, %d were already there.' % (written, skipped))
    if refused_total:
        say('%d pictures did not come out right and were left as they were.' % refused_total)
    say('Added %.1f GB. Took %.0f minutes.' % (grew / 1e9, (time.time() - started) / 60))
    say('Start the game to see them.')
    spare = sum(os.path.getsize(os.path.join(cache, n)) for n in os.listdir(cache))
    if spare:
        say('')
        say(r'Mods\HDTextureWork holds %.0f GB of working files. You can delete it'
            % (spare / 1e9))
        say('to get the space back, or keep it to make more maps quickly later.')


if __name__ == '__main__':
    main()
