"""Pick which graphics card does the enlarging, by timing them.

This is not a refinement. The upscaler's own default picked the integrated
Intel chip on the development machine even though an RTX 4080 was sitting next
to it, and the same work went from a tenth of a second per texture to twelve
seconds -- an hour's job turned into a day's. Nothing in the output says which
device was used, so the only symptom is that it is slow.

Names are not enough to decide: a laptop can have a discrete Intel Arc, and
"Graphics" appears in the name of fast and slow parts alike. So each device
enlarges the same small picture once and the quickest wins. It costs a few
seconds, once per machine, and the answer is remembered.
"""
import os
import re
import struct
import subprocess
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import progress as progress_module

DEVICE_LINE = re.compile(r'^\[(\d+)\s+(.*?)\]')
PROBE_SIZE = 768        # big enough to separate the cards, small enough to be quick
CPU = -1


def write_probe_png(path, size=PROBE_SIZE):
    """A small PNG to time the devices with. Content does not matter, size does."""
    row = bytes(bytearray([(x * 7) & 0xFF for x in range(size * 3)]))
    raw = b''.join(b'\0' + row for _ in range(size))

    def chunk(tag, body):
        data = tag + body
        return struct.pack('>I', len(body)) + data + struct.pack('>I', zlib.crc32(data))

    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n'
                           + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 1))
                           + chunk(b'IEND', b''))


def devices(tools, folder):
    """[(index, name)] as the upscaler itself reports them."""
    source = os.path.join(folder, 'probe_in')
    target = os.path.join(folder, 'probe_out')
    os.makedirs(source, exist_ok=True)
    os.makedirs(target, exist_ok=True)
    write_probe_png(os.path.join(source, 'probe.png'), 64)
    done = subprocess.run([tools.waifu2x, '-i', os.path.join(source, 'probe.png'),
                           '-o', os.path.join(target, 'probe.png'),
                           '-s', '2', '-n', '0', '-m', tools.model, '-v'],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    found = {}
    for line in done.stdout.decode('utf-8', 'replace').splitlines():
        match = DEVICE_LINE.match(line.strip())
        if match:
            found.setdefault(int(match.group(1)), match.group(2))
    return sorted(found.items())


def time_device(tools, folder, index):
    source = os.path.join(folder, 'probe_in')
    target = os.path.join(folder, 'probe_out')
    write_probe_png(os.path.join(source, 'probe.png'))
    started = time.time()
    done = subprocess.run([tools.waifu2x, '-i', os.path.join(source, 'probe.png'),
                           '-o', os.path.join(target, 'probe.png'),
                           '-s', '2', '-n', '0', '-m', tools.model, '-g', str(index)],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if done.returncode != 0:
        return None
    return time.time() - started


def choose(tools, folder, asked=None):
    """The device to use, remembered in the work folder after the first run."""
    note = os.path.join(folder, 'graphics_card.txt')
    if asked is not None:
        return asked, 'chosen by hand'
    if os.path.isfile(note):
        try:
            saved = open(note, encoding='utf-8').read().strip().split('\t')
            return int(saved[0]), saved[1] if len(saved) > 1 else 'remembered'
        except (ValueError, IndexError):
            pass

    available = devices(tools, folder)
    if not available:
        progress_module.say('   no graphics card found for this; using the processor,')
        progress_module.say('   which works but takes many hours.')
        return CPU, 'processor'

    if len(available) == 1:
        best, name = available[0][0], available[0][1]
    else:
        progress_module.say('   checking %d graphics devices' % len(available))
        timed = []
        for index, name in available:
            spent = time_device(tools, folder, index)
            if spent is not None:
                timed.append((spent, index, name))
        if not timed:
            return CPU, 'processor'
        timed.sort()
        best, name = timed[0][1], timed[0][2]

    with open(note, 'w', encoding='utf-8') as handle:
        handle.write('%d\t%s\n' % (best, name))
    return best, name
