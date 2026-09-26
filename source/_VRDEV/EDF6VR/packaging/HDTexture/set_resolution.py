"""Write ForceWidth/ForceHeight into EDF6VR.ini as a multiple of the normal size.

Line based on purpose. The settings file is full of comments explaining what
each key does, and a parser that understands INI files would drop every one of
them the first time somebody changed the picture size.

    python set_resolution.py <ini path> <multiplier>
"""
import os
import sys

BASE_WIDTH = 3840
BASE_HEIGHT = 2160
MOST = 2.0          # 7680x4320, the largest the plugin accepts
LEAST = 0.5


def set_key(lines, section, key, value):
    wanted = None
    here = None
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('[') and stripped.endswith(']'):
            here = stripped[1:-1].lower()
            continue
        if here != section.lower():
            continue
        name = stripped.split('=', 1)[0].strip().lower()
        if name == key.lower():
            wanted = index
    if wanted is None:
        raise SystemExit('%s is missing [%s] %s' % ('EDF6VR.ini', section, key))
    lines[wanted] = '%s=%s' % (key, value)
    return lines


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: set_resolution.py <ini> <multiplier>')
    path = sys.argv[1]
    try:
        scale = float(sys.argv[2].strip())
    except ValueError:
        print('That is not a number. Nothing was changed.')
        return 1
    if not LEAST <= scale <= MOST:
        print('The number must be between %s and %s. Nothing was changed.' % (LEAST, MOST))
        return 1

    # Both sides move together: the 16:9 shape is what the headset fit trims
    # down, and a different shape would change the field of view instead of the
    # sharpness.
    width = int(round(BASE_WIDTH * scale / 8.0)) * 8
    height = int(round(width * 9.0 / 16.0))
    height += height & 1

    with open(path, 'r', encoding='utf-8-sig') as handle:
        lines = handle.read().splitlines()
    lines = set_key(lines, 'Render', 'ForceWidth', width)
    lines = set_key(lines, 'Render', 'ForceHeight', height)
    temporary = path + '.new'
    with open(temporary, 'w', encoding='utf-8', newline='\r\n') as handle:
        handle.write('\n'.join(lines) + '\n')
    os.replace(temporary, path)

    print('')
    print('  Picture size is now %.2fx   (%d x %d)' % (scale, width, height))
    print('')
    print('  Start the game to use it.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
