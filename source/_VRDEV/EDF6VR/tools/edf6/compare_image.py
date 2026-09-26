"""Put two or three versions of one texture side by side so they can be judged.

texconv can write an uncompressed BMP and that is trivial to read, so nothing
outside the standard library is needed to build the comparison sheet. Each image
is drawn at the same size, nearest-neighbour, so the only difference on screen
is the difference in the images.

    python tools/edf6/compare_image.py out.png "label:a.bmp" "label:b.bmp" ...
"""
import struct
import sys
import zlib


def read_bmp(path):
    blob = open(path, 'rb').read()
    if blob[:2] != b'BM':
        raise ValueError('%s is not a BMP' % path)
    start = struct.unpack_from('<I', blob, 10)[0]
    header = struct.unpack_from('<I', blob, 14)[0]
    width, height = struct.unpack_from('<ii', blob, 18)
    planes, depth = struct.unpack_from('<HH', blob, 26)
    if planes != 1 or depth not in (24, 32):
        raise ValueError('%s: %d bit BMP not handled' % (path, depth))
    flip = height > 0
    height = abs(height)
    stride = (width * depth // 8 + 3) & ~3
    rows = []
    for y in range(height):
        at = start + y * stride
        row = bytearray()
        for x in range(width):
            pixel = at + x * (depth // 8)
            blue, green, red = blob[pixel], blob[pixel + 1], blob[pixel + 2]
            row += bytes((red, green, blue))
        rows.append(bytes(row))
    if flip:
        rows.reverse()
    _ = header
    return width, height, rows


def scale(rows, width, height, factor):
    out = []
    for y in range(height * factor):
        source = rows[y // factor]
        row = bytearray()
        for x in range(width * factor):
            at = (x // factor) * 3
            row += source[at:at + 3]
        out.append(bytes(row))
    return out


def write_png(path, width, height, rows):
    raw = b''.join(b'\0' + row for row in rows)
    def chunk(tag, body):
        data = tag + body
        return struct.pack('>I', len(body)) + data + struct.pack('>I', zlib.crc32(data))
    blob = (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9))
            + chunk(b'IEND', b''))
    open(path, 'wb').write(blob)


def main():
    out = sys.argv[1]
    panels = []
    for argument in sys.argv[2:]:
        _label, _, path = argument.partition(':')
        panels.append(read_bmp(path))
    biggest = max(w for w, _, _ in panels)
    tallest = max(h for _, h, _ in panels)
    scaled = []
    for width, height, rows in panels:
        factor = max(1, biggest // width)
        scaled.append((width * factor, height * factor, scale(rows, width, height, factor)))
    tallest = max(h for _, h, _ in scaled)
    gap = 8
    total = sum(w for w, _, _ in scaled) + gap * (len(scaled) - 1)
    sheet = []
    for y in range(tallest):
        row = bytearray()
        for index, (width, height, rows) in enumerate(scaled):
            if index:
                row += b'\x20\x20\x20' * gap
            row += rows[y] if y < height else b'\x00\x00\x00' * width
        sheet.append(bytes(row))
    write_png(out, total, tallest, sheet)
    print('%s: %dx%d, %d panels' % (out, total, tallest, len(scaled)))


if __name__ == '__main__':
    main()
