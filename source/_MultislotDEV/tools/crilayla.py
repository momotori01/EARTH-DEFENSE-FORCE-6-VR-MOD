"""CRILAYLA decompression (CRI's LZ variant used for compressed CPK entries). Read-only.

Layout: 'CRILAYLA', u32 uncompressed size, u32 compressed size; the compressed bit stream follows
and is read backwards from its end, and the first 0x100 bytes of the output are stored verbatim
after it.
"""
import struct

_LENGTH_BITS = (2, 3, 5, 8)


def decompress(data):
    if data[:8] != b'CRILAYLA':
        raise ValueError('not CRILAYLA data')
    size, compressed = struct.unpack_from('<II', data, 8)
    prefix = data[0x10 + compressed:0x10 + compressed + 0x100]
    out = bytearray(0x100 + size)
    out[:0x100] = prefix
    position = 0x10 + compressed - 1   # last byte of the bit stream
    pool = 0
    left = 0

    def bits(count):
        nonlocal position, pool, left
        value = 0
        produced = 0
        while produced < count:
            if left == 0:
                pool = data[position]
                position -= 1
                left = 8
            take = min(left, count - produced)
            value = (value << take) | ((pool >> (left - take)) & ((1 << take) - 1))
            left -= take
            produced += take
        return value

    end = 0x100 + size - 1
    written = 0
    while written < size:
        if bits(1):
            source = end - written + bits(13) + 3
            length = 3
            for width in _LENGTH_BITS:
                level = bits(width)
                length += level
                if level != (1 << width) - 1:
                    break
            else:
                while True:
                    level = bits(8)
                    length += level
                    if level != 255:
                        break
            for _ in range(length):
                out[end - written] = out[source]
                source -= 1
                written += 1
        else:
            out[end - written] = bits(8)
            written += 1
    return bytes(out)
