"""Make sure EDFModLoader will hand the game the files under Mods/.

The HD textures are loose archives under Mods/, and the only thing that reads
them is the loader's redirector, switched by Redirect in ModLoader.ini. The
package does not ship that file, so a first install runs on the loader's own
defaults, which have it on. A player with other mods has a ModLoader.ini of
their own; the one setting the texture pack needs is turned on there and
nothing else in the file is touched.

The reading rules are the loader's, not Python's configparser: it asks Windows
GetPrivateProfileStringW for [ModLoader] Redirect, default "True", and only a
case-insensitive "True" is on. Section and key names are case-insensitive, the
first match wins, and whitespace around the value is ignored. A missing file,
section or key is the default, which is on.
"""
import codecs
import os

SECTION = 'modloader'
KEY = 'redirect'


def _decode(raw):
    if raw.startswith(codecs.BOM_UTF16_LE):
        return raw[2:].decode('utf-16-le'), codecs.BOM_UTF16_LE, 'utf-16-le'
    if raw.startswith(codecs.BOM_UTF8):
        return raw[3:].decode('utf-8'), codecs.BOM_UTF8, 'utf-8'
    try:
        return raw.decode('utf-8'), b'', 'utf-8'
    except UnicodeDecodeError:
        return raw.decode('mbcs' if os.name == 'nt' else 'latin-1'), b'', \
            'mbcs' if os.name == 'nt' else 'latin-1'


def _find(lines):
    """Index of the line the loader would read Redirect from, or None."""
    inside = False
    seen_section = False
    for index, line in enumerate(lines):
        text = line.strip()
        if text.startswith('['):
            name = text[1:text.index(']')] if ']' in text else text[1:]
            if inside:
                return None
            inside = name.strip().lower() == SECTION and not seen_section
            seen_section = seen_section or inside
            continue
        if inside and '=' in text and not text.startswith(';'):
            if text.split('=', 1)[0].strip().lower() == KEY:
                return index
    return None


def _value(line):
    value = line.split('=', 1)[1].strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in '"\'':
        value = value[1:-1]
    return value


def redirect_on(text):
    lines = text.splitlines(keepends=True)
    index = _find(lines)
    return index is None or _value(lines[index]).lower() == 'true'


def switch_on(text):
    """The same text with Redirect turned on, or the text unchanged."""
    lines = text.splitlines(keepends=True)
    index = _find(lines)
    if index is None or _value(lines[index]).lower() == 'true':
        return text
    line = lines[index]
    ending = line[len(line.rstrip('\r\n')):]
    lines[index] = 'Redirect=True' + ending
    return ''.join(lines)


def ensure_redirect(root):
    """Returns 'default', 'on' or 'switched'."""
    path = os.path.join(root, 'ModLoader.ini')
    if not os.path.isfile(path):
        return 'default'
    raw = open(path, 'rb').read()
    text, bom, encoding = _decode(raw)
    if redirect_on(text):
        return 'on'
    temporary = path + '.part'
    with open(temporary, 'wb') as out:
        out.write(bom + switch_on(text).encode(encoding))
    os.replace(temporary, path)
    return 'switched'
