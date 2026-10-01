"""Lighter effects for the heaviest enemy fire in VR, made from the player's own game files.

The Kurul (E601) shotgun fires pellets that stick and glow pink for 2-2.5 s
before they explode, so in a big fight hundreds glow at once, and VR draws the
world -- every glow -- twice (2026-10-01: frames to 40 ms in a Kurul shotgun
fight). Each glowing pellet is two pictures drawn over each other: a ball and a
ring that keeps swelling around it. This drops the ring and keeps the ball, its
size and everything else; the explosion (the flash ring that shows the blast's
reach, the sparks, the light) is left as the game ships it. A first try scaled
the whole effect down and lost the punch and the sense of range (rejected).

The effect files (.esb, inside OBJECT/E601_SHOTGUN.EFARC) are a block of tagged
objects -- the tag is the CRC-32 of the type's name in capitals -- that point at
one another relative to where each object's fields start (read out of the game's
329 effect files, every one of which walks clean this way):

    0x04  where the list of top objects sits; each list is -2, count, entries
    root  ROOT? -> LIFE -> a list of what the effect draws, in order
    unit  0x2194205D: [order, TRANSFORM, material, colour/alpha]
          material MATERIALSPRITE -> [.., SPRITETYPE -> -1, "NAME.SSB"]

Dropping a picture is taking its unit out of LIFE's list: the list's count
goes down and the units after it move up a slot (their offsets are relative to
the list, not the slot, so they move unchanged). The unit itself stays in the
file, unreferenced, so nothing else has to move.

A copy of the archive with that change goes to Mods/OBJECT, where EDFModLoader
hands it to the game in place of Root.cpk's. Nothing of the game is shipped: the
copy is made from the installed Root.cpk. A note beside it (.edf6vr) marks it as
ours; --remove deletes only that. Standard library only (the embedded Python of
Mods/HDTexture runs it).

usage: python light_effects.py [game folder] [--remove]
"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.append(HERE)
import cpk      # noqa: E402
import rab      # noqa: E402
from crew_figures import read_file   # noqa: E402  (CRILAYLA-aware)

VERSION = 2
# (directory, archive, {effect: pictures to drop, each found exactly once})
TARGETS = [
    ('OBJECT', 'E601_SHOTGUN.EFARC', {'bullet.esb': ['THUNDERRING01.SSB']}),
]
# What version 1 wrote (a scaled-down E601_SHOTGUN.SGO); --remove and a new run clear it.
OLD = [('OBJECT', 'E601_SHOTGUN.SGO')]

MAGIC = 0x10F62EF4


def tag_of(name):
    return zlib.crc32(name.encode('ascii')) & 0xFFFFFFFF


LIFE, UNIT = tag_of('LIFE'), 0x2194205D
MATERIALSPRITE, SPRITETYPE = tag_of('MATERIALSPRITE'), tag_of('SPRITETYPE')
ARRAY, STRING = 0xFFFFFFFE, 0xFFFFFFFF


class Esb:
    """An effect file's objects, by the offset where each one's fields start."""

    def __init__(self, data):
        if len(data) < 16 or self.u32(data, 0) != MAGIC:
            raise ValueError('not an effect file')
        self.data = bytearray(data)

    @staticmethod
    def u32(buf, at):
        return struct.unpack_from('<I', buf, at)[0]

    def tag(self, body):
        if not 4 <= body <= len(self.data) - 4:
            raise ValueError('pointer out of the file: %#x' % body)
        return self.u32(self.data, body - 4)

    def field(self, body, index):
        """A pointer field: where the object it names starts, or None."""
        value = struct.unpack_from('<i', self.data, body + 4 * index)[0]
        return None if value == 0 else body + value

    def array(self, body):
        if self.tag(body) != ARRAY:
            raise ValueError('no list at %#x' % body)
        count = self.u32(self.data, body)
        return [body + struct.unpack_from('<i', self.data, body + 4 + 4 * k)[0] for k in range(count)]

    def string(self, body):
        if body is None or self.tag(body) != STRING:
            raise ValueError('no name at %#x' % (body or 0))
        return bytes(self.data[body:self.data.index(b'\0', body)]).decode('ascii')

    def lives(self):
        """Each top object's LIFE: (where its list of units starts, the units)."""
        out = []
        for top in self.array(self.u32(self.data, 4) + 4):
            life = self.field(top, 0)
            if life is None or self.tag(life) != LIFE:
                raise ValueError('top object without a LIFE at %#x' % top)
            units = self.field(life, 0)
            out.append((units, self.array(units)))
        return out

    def picture(self, unit):
        """The sprite a unit draws, by name, or None if it draws something else."""
        if self.tag(unit) != UNIT:
            return None
        material = self.field(unit, 2)
        if material is None or self.tag(material) != MATERIALSPRITE:
            return None
        sprite = self.field(material, 1)
        if sprite is None or self.tag(sprite) != SPRITETYPE:
            return None
        return self.string(self.field(sprite, 0)).upper()

    def drop(self, name):
        """Take the units drawing `name` out of their lists. Returns how many."""
        dropped = 0
        for units_at, units in self.lives():
            keep = [u for u in units if self.picture(u) != name]
            if len(keep) == len(units):
                continue
            struct.pack_into('<I', self.data, units_at, len(keep))
            for slot, unit in enumerate(keep):
                struct.pack_into('<i', self.data, units_at + 4 + 4 * slot, unit - units_at)
            dropped += len(units) - len(keep)
        return dropped


def ours(path):
    return os.path.isfile(path + '.edf6vr')


def remove(out_root, directory, name):
    path = os.path.join(out_root, directory, name)
    if not ours(path):
        return False
    if os.path.isfile(path):
        os.remove(path)
    os.remove(path + '.edf6vr')
    return True


def make(archive, directory, name, drops):
    """The archive with the pictures dropped, or a reason it was not made."""
    effects = rab.parse(read_file(archive, directory, name))
    for effect, pictures in drops.items():
        entry = effects.find(effect)
        if entry is None:
            return None, '%s has no %s' % (name, effect)
        esb = Esb(entry.data)
        for picture in pictures:
            count = esb.drop(picture.upper())
            if count != 1:
                return None, '%s/%s: %s found %d times' % (name, effect, picture, count)
        entry.replace(bytes(esb.data))
    return effects.to_bytes(), None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    game = args[0] if args else os.path.normpath(os.path.join(HERE, '..', '..', '..', '..'))
    mods = os.path.join(game, 'Mods')
    for directory, name in OLD:
        if remove(mods, directory, name):
            print('removed', name, '(version 1)')
    if '--remove' in sys.argv:
        for directory, name, _drops in TARGETS:
            if remove(mods, directory, name):
                print('removed', name)
        return 0
    root = os.path.join(game, 'Root.cpk')
    if not os.path.isfile(root):
        print('Root.cpk not found in', game)
        return 1
    archive = cpk.Cpk(root)
    failed = 0
    for directory, name, drops in TARGETS:
        target = os.path.join(mods, directory, name)
        if os.path.isfile(target) and not ours(target):
            print('%s: another mod already replaces it; left alone' % name)
            continue
        data, why = make(archive, directory, name, drops)
        if data is None:
            print('%s: nothing written' % why)
            failed = 1
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'wb') as handle:
            handle.write(data)
        with open(target + '.edf6vr', 'w') as handle:
            handle.write('EDF6VR light_effects %d\n' % VERSION)
        print('%s: lighter effects written to %s' % (name, target))
    return failed


if __name__ == '__main__':
    sys.exit(main())
