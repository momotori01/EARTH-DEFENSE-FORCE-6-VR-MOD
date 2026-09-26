"""Does the enlarged texture still look like the one it came from?

The dimensions, the format and the mip chain can all be right while the picture
is wrong, and that is not hypothetical: the integrated Intel chip that the
upscaler picked by default ran out of room on 2048-pixel textures and returned
solid black. Seven of them went into a city map, and the road, the pavement and
a vending machine rendered as black holes in the game. Nothing in the archive
was malformed; every structural check passed.

So the picture itself is checked. Each texture is decoded to eight pixels square
-- one batched call for all of them, the same tool that wrote them -- and the
average is compared with the average of the source. A texture that was not dark
and came back dark did not survive, whatever the reason, and is dropped rather
than shipped.

Brightness is not the whole story. BC1 stores either four colours or three plus
transparent black, and a texture whose upscale came back blank in places is
written with the transparent code over those places -- which the game draws as
black while the average stays bright. The large health item shipped that way
(39% of it transparent against none in the source) and rendered as a black box.
So the transparent share is measured as well, straight from the compressed
bytes, and compared with the source: more than the source, by more than a
hundredth of the picture, is a failure too.
"""
import array
import os
import struct

# Below this the source was already almost black, and a ratio against it would
# be noise rather than a measurement.
DARK = 8.0
# How much of the original brightness has to survive. A 2x upscale changes the
# average by a percent or two; anything approaching a third has failed.
KEEP = 0.35


def mean_bmp(path):
    """Average of all channels of a small uncompressed BMP.

    None when there is nothing to measure. texconv quietly declines a handful of
    formats, so the file may simply not be there, and a build must not stop for
    that -- the structural checks still apply to those textures.
    """
    try:
        blob = open(path, 'rb').read()
    except OSError:
        return None
    if len(blob) < 54 or blob[:2] != b'BM':
        return None
    start = struct.unpack_from('<I', blob, 10)[0]
    width, height = struct.unpack_from('<ii', blob, 18)
    depth = struct.unpack_from('<H', blob, 28)[0]
    if depth not in (24, 32) or not width or not height:
        return None
    height = abs(height)
    stride = (width * depth // 8 + 3) & ~3
    step = depth // 8
    total = count = 0
    for y in range(height):
        row = start + y * stride
        for x in range(width):
            at = row + x * step
            if at + 3 > len(blob):
                return None
            total += blob[at] + blob[at + 1] + blob[at + 2]
            count += 3
    return total / float(count) if count else None


# BC1: a block whose first colour is not greater than its second holds three
# colours and transparent black, and index 3 is that transparent code. BC1 is
# the only format this looks at: it is all or nothing, so the count means what
# it says. Glass and anything else that fades keeps its alpha in BC2, BC3 or
# BC7, which are left alone.
TRANSPARENT_ALLOWANCE = 0.01
# A fence or a leaf is mostly transparent on purpose. Losing that would fill
# the holes in, so a result has to keep at least half of what its source had.
TRANSPARENT_KEEP = 0.5
_THREES = bytes(sum(1 for pair in range(8) if (value >> (2 * pair)) & 3 == 3)
                for value in range(65536))


def transparent_share(path):
    """How much of a BC1 file's top mip is the transparent code, or None.

    None for anything that is not BC1: other formats keep alpha of their own and
    say nothing about a failed upscale.
    """
    try:
        blob = open(path, 'rb').read()
    except OSError:
        return None
    if len(blob) < 128 or blob[:4] != b'DDS ' or blob[84:88] != b'DXT1':
        return None
    height, width = struct.unpack_from('<II', blob, 12)
    blocks = max(1, width // 4) * max(1, height // 4)
    body = memoryview(blob)[128:]
    if len(body) < blocks * 8:
        return None
    words = array.array('H')
    words.frombytes(body[:blocks * 8])
    if struct.pack('=H', 1) != struct.pack('<H', 1):
        words.byteswap()
    transparent = 0
    for block in range(0, blocks * 4, 4):
        if words[block] <= words[block + 1]:
            transparent += _THREES[words[block + 2]] + _THREES[words[block + 3]]
    return transparent / float(blocks * 16)


def survivors(tools, sources, results, work, stems):
    """The stems whose picture came through, and a list of the ones that did not.

    `sources` and `results` are folders holding <stem>.dds on each side.
    """
    before = os.path.join(work, 'check_before')
    after = os.path.join(work, 'check_after')
    for folder in (before, after):
        if not os.path.isdir(folder):
            os.makedirs(folder)
        for name in os.listdir(folder):
            os.remove(os.path.join(folder, name))

    def thumbnails(folder, target):
        files = [os.path.join(folder, n) for n in os.listdir(folder)
                 if n.lower().endswith('.dds')]
        if files:
            tools.to_thumbnails(files, target)

    thumbnails(sources, before)
    thumbnails(results, after)

    kept, lost = [], []
    for stem in stems:
        was = mean_bmp(os.path.join(before, stem + '.bmp'))
        now = mean_bmp(os.path.join(after, stem + '.bmp'))
        source_clear = transparent_share(os.path.join(sources, stem + '.dds'))
        result_clear = transparent_share(os.path.join(results, stem + '.dds'))
        if (source_clear is not None and result_clear is not None
                and result_clear > source_clear + TRANSPARENT_ALLOWANCE):
            lost.append(stem)          # blank patches, which the game draws black
        elif (source_clear is not None and result_clear is not None
                and source_clear > TRANSPARENT_ALLOWANCE
                and result_clear < source_clear * TRANSPARENT_KEEP):
            lost.append(stem)          # the holes filled in
        elif was is None or now is None:
            kept.append(stem)          # nothing measurable; structure already checked
        elif was > DARK and now < was * KEEP:
            lost.append(stem)
        else:
            kept.append(stem)
    return kept, lost
