#!/usr/bin/env python3
"""Link the game's actual Vulkan/runtime objects with a headless diagnostic host."""
from pathlib import Path
import argparse
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--file-smoke', action='store_true')
parser.add_argument('--clock-smoke', action='store_true')
parser.add_argument('--input-smoke', action='store_true')
parser.add_argument('--audio-smoke', action='store_true')
parser.add_argument('--vertex-smoke', action='store_true')
args = parser.parse_args()
build = root / 'out/build/linux-release'
out = root / 'out/probe'
out.mkdir(parents=True, exist_ok=True)
sdk = root / 'sdk'
(out / 'vulkan_probe.h').write_text((sdk / 'runtime/gpu/vulkan.h').read_text().replace('private:', 'public:'))
line = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'LARush'], text=True).splitlines()[-1]
objects = [str(build / arg) for arg in shlex.split(line) if arg.endswith('.o') and not any(part in arg for part in ['/runtime/app/', '/audio/sdl_audio.cpp.o', '/core/input.cpp.o', '/core/keyboard_pad.cpp.o'])]
sources = [sdk / 'harness' / name for name in ['app_headless.cpp', 'main_test.cpp', 'pad_script.cpp']]
sources += [root / 'tests/probe_backend.cpp', root / 'tests/probe_trace.cpp']
sources.append(root / ('tests/audio_smoke.cpp' if args.audio_smoke else 'tests/probe_audio.cpp'))
if args.file_smoke:
    sources.append(root / 'tests/file_io_smoke.cpp')
elif args.clock_smoke:
    sources.append(root / 'tests/clock_smoke.cpp')
elif args.input_smoke:
    sources.append(root / 'tests/input_smoke.cpp')
elif args.vertex_smoke:
    sources.append(root / 'tests/vertex_input_smoke.cpp')
for source in sources:
    obj = out / (source.stem + '.o')
    command = ['clang++', '-std=c++20', '-O1', '-g', '-fno-strict-aliasing', '-D_stricmp=strcasecmp', '-D_strnicmp=strncasecmp']
    for inc in [sdk / 'runtime', root / 'generated/game', sdk / 'harness', sdk / 'runtime/platform/compat', out]:
        command += ['-I', str(inc)]
    subprocess.run(command + ['-c', str(source), '-o', str(obj)], check=True)
    objects.append(str(obj))
exe = out / ('LARushFileSmoke' if args.file_smoke else 'LARushClockSmoke' if args.clock_smoke else 'LARushInputSmoke' if args.input_smoke else 'LARushAudioSmoke' if args.audio_smoke else 'LARushVertexSmoke' if args.vertex_smoke else 'LARushProbe')
subprocess.run(['clang++', '-fuse-ld=lld', '-g', '-Wl,--wrap=_ZN3gpu13CreateBackendEv', '-Wl,--wrap=sub_00263560', '-Wl,--wrap=sub_00188A90', '-Wl,--wrap=sub_0029AC5F', '-Wl,--wrap=sub_0029AE99', '-Wl,--wrap=sub_0018AE70', '-Wl,--wrap=sub_001B0105', '-Wl,-rpath,' + str(build), '-o', str(exe)] + objects + ['-lSDL3', '-lvulkan', '-ldl', '-lpthread'], check=True)
print(exe)
