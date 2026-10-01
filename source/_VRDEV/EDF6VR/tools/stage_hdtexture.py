"""Assemble Mods/HDTexture: the scripts, the two tools and a Python to run them.

The pack builder is Python, and a mod cannot ask people to install Python, so
the official embeddable build travels with it. It is 22 MB unpacked, needs no
installer, touches no registry and no other Python on the machine, and the whole
thing lives in one folder that can be deleted.

    python tools/stage_hdtexture.py [--deploy]

--deploy also copies the result into the game folder, next to the plugin, which
is what the two .bat files expect to find.
"""
import argparse
import hashlib
import os
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.abspath(os.path.join(ROOT, '..', '..'))
STAGE = os.path.join(ROOT, 'dist', 'HDTexture')

SCRIPTS = ('hd_textures.py', 'cpk.py', 'rab.py', 'dds.py', 'progress.py', 'gpu.py', 'check.py',
           'loader_config.py',
           'verify_pack.py',
           # The Proteus crew figures (Mods/Plugins/EDF6VRCrew), made from the
           # player's own Root.cpk by the settings program, like the HD pack.
           'crew_figures.py',
           # The lighter Kurul shots (Mods/OBJECT/E601_SHOTGUN.EFARC), made or
           # undone by the settings program to match [VR] LightEnemyEffects.
           'light_effects.py')
PYTHON_ZIP = os.path.join(ROOT, 'tools', 'upscale', 'python-embed.zip')
# The model that fits this job. The other two in the release are for line art;
# shipping them would treble the download for pictures of concrete.
MODEL = 'models-upconv_7_photo'


def copy_tree(source, target):
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(source, target)


def build():
    if os.path.isdir(STAGE):
        shutil.rmtree(STAGE)
    os.makedirs(STAGE)

    for name in SCRIPTS:
        shutil.copy(os.path.join(ROOT, 'tools', 'edf6', name), os.path.join(STAGE, name))
    shutil.copy(os.path.join(ROOT, 'packaging', 'HDTexture', 'set_resolution.py'),
                os.path.join(STAGE, 'set_resolution.py'))

    upscale = os.path.join(ROOT, 'tools', 'upscale')
    shutil.copy(os.path.join(upscale, 'texconv.exe'), os.path.join(STAGE, 'texconv.exe'))
    waifu = os.path.join(STAGE, 'waifu2x')
    os.makedirs(waifu)
    for name in ('waifu2x-ncnn-vulkan.exe', 'vcomp140.dll'):
        shutil.copy(os.path.join(upscale, 'waifu2x', name), os.path.join(waifu, name))
    copy_tree(os.path.join(upscale, 'waifu2x', MODEL), os.path.join(waifu, MODEL))

    python = os.path.join(STAGE, 'python')
    os.makedirs(python)
    with zipfile.ZipFile(PYTHON_ZIP) as archive:
        archive.extractall(python)

    licenses = os.path.join(STAGE, 'Licenses')
    os.makedirs(licenses)
    shutil.copy(os.path.join(upscale, 'DirectXTex-LICENSE.txt'),
                os.path.join(licenses, 'DirectXTex-LICENSE.txt'))
    shutil.copy(os.path.join(upscale, 'waifu2x', 'waifu2x-ncnn-vulkan-LICENSE.txt'),
                os.path.join(licenses, 'waifu2x-ncnn-vulkan-LICENSE.txt'))
    shutil.copy(os.path.join(python, 'LICENSE.txt'),
                os.path.join(licenses, 'Python-LICENSE.txt'))

    total = 0
    count = 0
    for base, _directories, names in os.walk(STAGE):
        for name in names:
            total += os.path.getsize(os.path.join(base, name))
            count += 1
    print('staged %s: %d files, %.1f MB' % (STAGE, count, total / 1e6))
    return STAGE


def deploy():
    # File by file, skipping what is already identical: a pack build running out
    # of this same folder has its Python mapped into memory, and Windows will
    # not let a running image be replaced. Those files never differ anyway --
    # only the scripts change between builds -- so the copy that matters still
    # lands. Anything genuinely different and locked is reported rather than
    # swallowed.
    target = os.path.join(GAME, 'Mods', 'HDTexture')
    copied, locked = 0, []
    for base, _directories, names in os.walk(STAGE):
        here = os.path.join(target, os.path.relpath(base, STAGE)) if base != STAGE else target
        os.makedirs(here, exist_ok=True)
        for name in names:
            source, destination = os.path.join(base, name), os.path.join(here, name)
            if os.path.isfile(destination) and \
               open(source, 'rb').read() == open(destination, 'rb').read():
                continue
            try:
                shutil.copy(source, destination)
                copied += 1
            except PermissionError:
                locked.append(destination)
    if locked:
        print('in use, not replaced:')
        for path in locked:
            print('  %s' % path)
    print('copied %d changed files' % copied)
    for name in ('HD_Texture_2x.bat', 'Set_Resolution.bat'):
        shutil.copy(os.path.join(ROOT, 'packaging', name), os.path.join(GAME, name))
    print('deployed to %s and the two .bat files to %s' % (target, GAME))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--deploy', action='store_true')
    options = parser.parse_args()
    build()
    if options.deploy:
        if not os.path.isfile(os.path.join(GAME, 'EDF6.exe')):
            raise SystemExit('game folder not found: %s' % GAME)
        deploy()
    return 0


if __name__ == '__main__':
    sys.exit(main())
