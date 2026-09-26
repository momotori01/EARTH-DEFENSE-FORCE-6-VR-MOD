"""Fetch the three third-party programs the HD texture pack is built with.

They are not in the repository for the same reason the OpenXR SDK checkout is
not: they are eighty megabytes of somebody else's release binaries. Pinned URL
and SHA-256 for each, so a later run either produces exactly these bytes or
stops. Everything downloaded here is redistributable, and its licence travels
into Mods/HDTexture/Licenses in the shipped package.

    python tools/fetch_upscale_tools.py

  texconv      DirectXTex, MIT, Microsoft.
               Decodes and encodes BC1/BC3/BC5/BC7 and builds mip chains. It
               reproduces this game's DDS headers byte for byte apart from the
               size fields, which is why the pack can put a texture back into an
               archive and have the file look the way the game wrote it.
  waifu2x      waifu2x-ncnn-vulkan, MIT, nihui; models from nagadomi/waifu2x.
               Doubles at native 2x rather than 4x then down, runs from the
               command line on a whole folder at once, and falls back to the CPU
               with -g -1. Only the photo model ships; the two anime models in
               the release are for line art and would treble the download.
  python       CPython embeddable build, PSF licence, python.org.
               The builder is Python and a mod cannot ask people to install
               Python. No installer, no registry, one folder.
"""
import hashlib
import os
import shutil
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(HERE, 'upscale')

DOWNLOADS = (
    ('texconv.exe',
     'https://github.com/microsoft/DirectXTex/releases/download/may2026/texconv.exe',
     'dcfdec10244e02cf5037fba089c55fb7e1326b1c8181742d77d15fa5cb5eef06'),
    ('DirectXTex-LICENSE.txt',
     'https://raw.githubusercontent.com/microsoft/DirectXTex/main/LICENSE',
     None),
    ('waifu2x.zip',
     'https://github.com/nihui/waifu2x-ncnn-vulkan/releases/download/20250915/'
     'waifu2x-ncnn-vulkan-20250915-windows.zip',
     '7425be94b94e4c8f37a1e433ac0e0100c43790e2c37418f4b65d8235adfbdc87'),
    ('python-embed.zip',
     'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip',
     '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'),
)

WAIFU2X_INSIDE = 'waifu2x-ncnn-vulkan-20250915-windows'
WAIFU2X_FILES = ('waifu2x-ncnn-vulkan.exe', 'vcomp140.dll')
WAIFU2X_MODELS = ('models-upconv_7_photo', 'models-cunet')


def fetch(name, url, digest):
    path = os.path.join(TARGET, name)
    if os.path.isfile(path) and digest and sha256(path) == digest:
        print('  have %s' % name)
        return path
    print('  fetching %s' % name)
    with urllib.request.urlopen(url, timeout=300) as source:
        data = source.read()
    if digest:
        got = hashlib.sha256(data).hexdigest()
        if got != digest:
            raise SystemExit('%s: expected %s, got %s' % (name, digest, got))
    open(path, 'wb').write(data)
    return path


def sha256(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def main():
    os.makedirs(TARGET, exist_ok=True)
    for name, url, digest in DOWNLOADS:
        fetch(name, url, digest)

    waifu = os.path.join(TARGET, 'waifu2x')
    if os.path.isdir(waifu):
        shutil.rmtree(waifu)
    os.makedirs(waifu)
    with zipfile.ZipFile(os.path.join(TARGET, 'waifu2x.zip')) as archive:
        for name in WAIFU2X_FILES:
            with archive.open('%s/%s' % (WAIFU2X_INSIDE, name)) as source:
                open(os.path.join(waifu, name), 'wb').write(source.read())
        with archive.open('%s/LICENSE' % WAIFU2X_INSIDE) as source:
            open(os.path.join(waifu, 'waifu2x-ncnn-vulkan-LICENSE.txt'), 'wb').write(source.read())
        for entry in archive.infolist():
            parts = entry.filename.split('/')
            if len(parts) == 3 and parts[1] in WAIFU2X_MODELS and parts[2]:
                folder = os.path.join(waifu, parts[1])
                os.makedirs(folder, exist_ok=True)
                with archive.open(entry) as source:
                    open(os.path.join(folder, parts[2]), 'wb').write(source.read())
    print('ready: %s' % TARGET)
    return 0


if __name__ == '__main__':
    sys.exit(main())
