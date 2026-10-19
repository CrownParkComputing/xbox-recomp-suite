#!/usr/bin/env python3
"""Link PGR's compiled Linux runtime with a deterministic audio regression entry."""
import argparse
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, default=root / 'out/build/linux-release')
parser.add_argument('--run', action='store_true')
args = parser.parse_args()
build = args.build.resolve()
sdk = root / 'sdk'
out = root / 'out/audio-smoke'
out.mkdir(parents=True, exist_ok=True)
line = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'PGR1'], text=True).splitlines()[-1]
objects = [str(build / arg) for arg in shlex.split(line) if arg.endswith('.o') and not any(part in arg for part in
           ['/runtime/app/', '/audio/sdl_audio.cpp.o', '/core/input.cpp.o', '/core/keyboard_pad.cpp.o'])]
sources = [sdk / 'harness' / name for name in ['app_headless.cpp', 'main_test.cpp', 'pad_script.cpp']]
sources += [root / 'tests/audio_host.cpp', root / 'tests/audio_smoke.cpp']
for source in sources:
    obj = out / (source.stem + '.o')
    command = ['clang++', '-std=c++20', '-O1', '-g', '-fno-strict-aliasing',
               '-D_stricmp=strcasecmp', '-D_strnicmp=strncasecmp']
    for include in [sdk / 'runtime', root / 'generated/game', sdk / 'harness', sdk / 'runtime/platform/compat']:
        command += ['-I', str(include)]
    subprocess.run(command + ['-c', str(source), '-o', str(obj)], check=True)
    objects.append(str(obj))
exe = out / 'PGR1AudioSmoke'
subprocess.run(['clang++', '-fuse-ld=lld', '-g', '-Wl,-rpath,' + str(build), '-o', str(exe)] + objects +
               ['-lSDL3', '-lvulkan', '-lavformat', '-lavcodec', '-lswscale', '-lswresample', '-lavutil', '-ldl', '-lpthread'], check=True)
print(exe, flush=True)
if args.run:
    subprocess.run([str(exe), str(root / 'assets')], cwd=out, check=True)
