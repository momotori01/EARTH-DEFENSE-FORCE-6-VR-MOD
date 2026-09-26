"""Enough of the DDS header to decide what a texture is and to put it back the
way it was found.

EDF6 stores plain DDS files, so there is nothing exotic here: a legacy header
with a FourCC for BC1/BC3/BC5, or the DX10 extension carrying a DXGI format.
What matters for a texture pass is only which of those it was, because the
replacement must come back in the same format. A colour map that returns as a
normal-map format, or an opaque BC1 that returns with an alpha channel, is a
visible fault in the game rather than an error anyone sees here.
"""
import struct

HEADER = 128
DX10_HEADER = 148
# Formats with an alpha channel of their own (BC1's is a single transparent
# code, and stays as it was).
HAS_ALPHA = frozenset(('BC2_UNORM', 'BC2_UNORM_SRGB', 'BC3_UNORM', 'BC3_UNORM_SRGB',
                       'BC7_UNORM', 'BC7_UNORM_SRGB', 'R8G8B8A8_UNORM', 'R8G8B8A8_UNORM_SRGB',
                       'B8G8R8A8_UNORM'))

DXGI_NAMES = {
    28: 'R8G8B8A8_UNORM', 29: 'R8G8B8A8_UNORM_SRGB',
    71: 'BC1_UNORM', 72: 'BC1_UNORM_SRGB',
    74: 'BC2_UNORM', 75: 'BC2_UNORM_SRGB',
    77: 'BC3_UNORM', 78: 'BC3_UNORM_SRGB',
    80: 'BC4_UNORM', 81: 'BC4_SNORM',
    83: 'BC5_UNORM', 84: 'BC5_SNORM',
    87: 'B8G8R8A8_UNORM', 91: 'B8G8R8A8_UNORM_SRGB',
    94: 'BC6H_UF16', 95: 'BC6H_SF16',
    98: 'BC7_UNORM', 99: 'BC7_UNORM_SRGB',
}

# What a FourCC means in texconv's vocabulary.
FOURCC_NAMES = {
    b'DXT1': 'BC1_UNORM', b'DXT3': 'BC2_UNORM', b'DXT5': 'BC3_UNORM',
    b'ATI1': 'BC4_UNORM', b'BC4U': 'BC4_UNORM', b'BC4S': 'BC4_SNORM',
    b'ATI2': 'BC5_UNORM', b'BC5U': 'BC5_UNORM', b'BC5S': 'BC5_SNORM',
}

# Formats that carry a picture rather than a surface property. Only these are
# worth handing to an upscaler: everything else encodes directions, coverage or
# roughness, where invented detail is simply wrong data.
COLOUR_FORMATS = frozenset(('BC1_UNORM', 'BC1_UNORM_SRGB', 'BC2_UNORM', 'BC2_UNORM_SRGB',
                            'BC3_UNORM', 'BC3_UNORM_SRGB', 'BC7_UNORM', 'BC7_UNORM_SRGB',
                            'R8G8B8A8_UNORM', 'R8G8B8A8_UNORM_SRGB',
                            'B8G8R8A8_UNORM', 'B8G8R8A8_UNORM_SRGB'))


class Info:
    __slots__ = ('width', 'height', 'mips', 'fourcc', 'format', 'dx10', 'ok')

    def __init__(self, width=0, height=0, mips=0, fourcc=b'', format='', dx10=False, ok=False):
        self.width = width
        self.height = height
        self.mips = mips
        self.fourcc = fourcc
        self.format = format
        self.dx10 = dx10
        self.ok = ok

    @property
    def colour(self):
        return self.format in COLOUR_FORMATS

    def __repr__(self):
        return '%dx%d mips=%d %s%s' % (self.width, self.height, self.mips, self.format,
                                       ' (dx10)' if self.dx10 else '')


def read(blob):
    """Header facts. `ok` is false for anything that is not a DDS we understand."""
    if len(blob) < HEADER or blob[:4] != b'DDS ' or struct.unpack_from('<I', blob, 4)[0] != 124:
        return Info()
    height, width = struct.unpack_from('<II', blob, 12)
    mips = struct.unpack_from('<I', blob, 28)[0]
    # Cube maps and volumes hold several surfaces in one file; doubling one is a
    # different job with different rules, and there is no reason to attempt it.
    if struct.unpack_from('<I', blob, 112)[0] & 0x00200000:
        return Info()
    pixel_flags = struct.unpack_from('<I', blob, 80)[0]
    fourcc = blob[84:88]
    if pixel_flags & 0x4:  # DDPF_FOURCC
        if fourcc == b'DX10':
            if len(blob) < DX10_HEADER:
                return Info()
            dxgi = struct.unpack_from('<I', blob, 128)[0]
            name = DXGI_NAMES.get(dxgi, 'DXGI_%d' % dxgi)
            return Info(width, height, mips, fourcc, name, True, True)
        name = FOURCC_NAMES.get(fourcc)
        if not name:
            return Info(width, height, mips, fourcc, fourcc.decode('ascii', 'replace'), False, False)
        return Info(width, height, mips, fourcc, name, False, True)
    if pixel_flags & 0x40:  # DDPF_RGB
        bits = struct.unpack_from('<I', blob, 88)[0]
        name = {32: 'B8G8R8A8_UNORM', 24: 'B8G8R8X8_UNORM'}.get(bits)
        return Info(width, height, mips, b'', name or 'RGB_%d' % bits, False, bool(name))
    return Info(width, height, mips, fourcc, 'unknown', False, False)


def texconv_arguments(info):
    """The flags that reproduce this format, or None when it should not be rebuilt."""
    if not info.ok or info.format.startswith(('DXGI_', 'RGB_')):
        return None
    arguments = ['-f', info.format, '-m', '0']
    # Mips made with the alpha apart from the colour. Left to itself texconv
    # weights the colour by the alpha while it shrinks, and in these textures
    # the alpha is often a number the shader reads rather than a transparency:
    # the Air Raider's radio has an alpha of about 2/255 in its RSO map, and
    # every mip after the first came back nearly black, so the radio was right
    # against the eye and a black field with bright specks anywhere else.
    if info.format in HAS_ALPHA:
        arguments.append('-sepalpha')
    if info.dx10:
        arguments.append('-dx10')
    return arguments
