#!/usr/bin/env python3
"""Build an Android port from the source suite, without packaging game files."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from prepare import ROOT, PORTS, CHIHIRO, prepare, image_for, verify_image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("title", choices=PORTS, nargs="?", default="Splashdown")
parser.add_argument("--jobs", default="3")
parser.add_argument("--stage-only", action="store_true", help="Prepare Java and per-port sources without game data")
parser.add_argument("--dependencies-only", action="store_true", help="Build SDL/glslang without game data")
parser.add_argument("--deps-build", type=Path, help="Reuse an existing compatible Android _deps build directory")
args = parser.parse_args()
title = args.title
label, slug, target = PORTS[title]
source = ROOT / "ports" / title
stage = ROOT / ".local/android" / title
sdk = prepare(title)
android = stage / "android"
main = android / "app/src/main"
sdk_root = Path(os.environ.get("ANDROID_SDK_ROOT", os.environ.get("ANDROID_HOME", "/opt/android-sdk")))
ndk = Path(os.environ.get("NDK", str(sdk_root / "ndk/28.2.13676358")))
dep_project = ROOT / ".local/android-dependencies"
dep_build = args.deps_build.resolve() if args.deps_build else dep_project / "_deps"
toolchain = ["-DCMAKE_TOOLCHAIN_FILE=" + str(ndk / "build/cmake/android.toolchain.cmake"),
             "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-33", "-DANDROID_STL=c++_shared",
             "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]

if not args.stage_only and not args.dependencies_only:
    for required in (source / "assets" / image_for(title), source / "generated/game/x86_func_mapping.cpp"):
        if not required.exists():
            parser.error(f"Missing {required.relative_to(ROOT)}; provide your game and run tools/build_linux.py {title} --generate-only first")
    verify_image(title)

if not args.stage_only:
    if args.deps_build:
        if not (dep_build / "sdl3-build/libSDL3.so").exists():
            parser.error("--deps-build must contain sdl3-build/libSDL3.so and the matching glslang/SPIRV libraries")
    else:
        subprocess.run(["cmake", "-G", "Ninja", "-S", str(ROOT / "android/dependencies"), "-B", str(dep_project)] + toolchain, check=True)
        subprocess.run(["cmake", "--build", str(dep_project), "--target", "SDL3-shared", "glslang", "SPIRV", "glslang-default-resource-limits", "-j", args.jobs], check=True)
    if args.dependencies_only:
        print(dep_build)
        raise SystemExit(0)

(main / "java").mkdir(parents=True, exist_ok=True)
for package in ("org/libsdl", "org/xberecomp"):
    shutil.copytree(ROOT / "android/app/src/main/java" / package, main / "java" / package, dirs_exist_ok=True)
package = "com." + slug + ".recomp"
java = main / "java" / package.replace(".", "/")
java.mkdir(parents=True, exist_ok=True)
for activity in ("LauncherActivity", "GameActivity"):
    (java / (activity + ".java")).write_text(f"package {package};\npublic class {activity} extends org.xberecomp.{activity} {{}}\n")
shutil.copytree(ROOT / "android/app/src/main/res", main / "res", dirs_exist_ok=True)
manifest = (ROOT / "android/app/src/main/AndroidManifest.xml").read_text()
manifest = manifest.replace("com.splashdown.recomp", package).replace('android:label="Splashdown"', f'android:label="{label}"')
if title in CHIHIRO:
    info = CHIHIRO[title]
    metadata = (f'<meta-data android:name="org.xberecomp.GAME_XBE" android:value="{info["xbe"]}" />\n'
                f'        <meta-data android:name="org.xberecomp.CONTROLS" android:value="{info["controls"]}" />\n'
                f'        <meta-data android:name="org.xberecomp.FOLDER_ONLY" android:value="{str(info["folder_only"]).lower()}" />\n        ')
    manifest = manifest.replace('<meta-data android:name="SDL_ENV.XBE_CAPTURE_DIR"', metadata + '<meta-data android:name="SDL_ENV.XBE_CAPTURE_DIR"')
(main / "AndroidManifest.xml").write_text(manifest)
script = (ROOT / "android/package.sh").read_text().replace("com.splashdown.recomp", package).replace("Splashdown-arm64", title + "-arm64")
(android / "package.sh").write_text(script)

cmake_args = []
if title == "BLACK":
    sys.path.insert(0, str(ROOT / "android/ports"))
    from black_vulkan import stage_vulkan
    cmake_args.extend(stage_vulkan(source, sdk, stage))
    render = (source / "src/d3d/render_thread.cpp").read_text().replace("#include <smmintrin.h>", "#include <arm_neon.h>")
    start = render.index("bool Same(const uint8_t*")
    end = render.index("constexpr size_t kPiece", start)
    render = render[:start] + '''bool Same(const uint8_t* a, const uint8_t* b, size_t n) {
    size_t i = 0;
    uint8x16_t diff = vdupq_n_u8(0);
    for (; i + 16 <= n; i += 16)
        diff = vorrq_u8(diff, veorq_u8(vld1q_u8(a + i), vld1q_u8(b + i)));
    if (vmaxvq_u8(diff)) return false;
    return i == n || !std::memcmp(a + i, b + i, n - i);
}

''' + render[end:]
    start = render.index("bool SamePiece(const uint8_t*")
    end = render.index("using Clock", start)
    render = render[:start] + '''bool SamePiece(const uint8_t* a, const uint8_t* b) { return Same(a, b, kPiece); }

''' + render[end:]
    (stage / "render_thread.cpp").write_text(render)
    cmake_args.append("-DXBE_BLACK_RENDER=" + str(stage / "render_thread.cpp"))
    files = (source / "src/common/game_files.cpp").read_text()
    files = files.replace("#include <fstream>", "#include <fstream>\n#include <cstdlib>\n#include <climits>\n#include <cerrno>")
    marker = "    // the first copy with this xbe wins"
    # Keep BLACK's European executable conversion when opening external media.
    selected = '''    std::vector<uint8_t> selected_xbe;
    auto selected = [&](const kernel::Disc* disc) {
        if (!disc->Load(IMAGE_XBE, selected_xbe)) return false;
        if (Sha1(selected_xbe) == kEuropeSha1 && !ConvertEurope(selected_xbe)) return false;
        if (Sha1(selected_xbe) != kImageSha1) return false;
        game = {assets, std::move(selected_xbe)};
        LOGR("Game: %s from selected media %s", IMAGE_XBE, disc->Path().string().c_str());
        return true;
    };
    if (const auto* mounted = kernel::MountedDisc()) return selected(mounted);
    if (const char* descriptor = std::getenv("XBE_DISC_FD"); descriptor && *descriptor) {
        char* end = nullptr; errno = 0;
        const long fd = std::strtol(descriptor, &end, 10);
        if (errno || !end || *end || fd < 0 || fd > INT_MAX) return false;
        auto disc = std::make_unique<kernel::Disc>(); std::string error;
        if (!disc->OpenFd(int(fd), "Selected game image", error) || !selected(disc.get())) return false;
        kernel::MountDisc(std::move(disc)); return true;
    }

'''
    files = files.replace(marker, selected + marker)
    (stage / "game_files.cpp").write_text(files)
    cmake_args.append("-DXBE_BLACK_FILES=" + str(stage / "game_files.cpp"))

if not args.stage_only:
    build = stage / "build-native"
    subprocess.run(["cmake", "-G", "Ninja", "-S", str(ROOT / "android/ports"), "-B", str(build),
        "-DXBE_REPO_DIR=" + str(source), "-DXBE_SDK_DIR=" + str(sdk),
        "-DXBE_DEPS_SOURCE=" + str(ROOT / "third_party"), "-DXBE_DEPS_BUILD=" + str(dep_build),
        "-DXBE_TITLE=" + title, "-DXBE_GAME_XBE=" + image_for(title),
        "-DXBE_INPUT_DEFINE=" + CHIHIRO.get(title, {}).get("input", "")] + toolchain + cmake_args, check=True)
    subprocess.run(["cmake", "--build", str(build), "--target", "main", "-j", args.jobs], check=True)
    libs = main / "jniLibs/arm64-v8a"
    libs.mkdir(parents=True, exist_ok=True)
    shutil.copy2(build / "libmain.so", libs)
    shutil.copy2(dep_build / "sdl3-build/libSDL3.so", libs)
    shutil.copy2(ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so", libs)
    env = os.environ.copy()
    keys = ROOT / ".local/keys"
    keys.mkdir(parents=True, exist_ok=True)
    env["KEYSTORE"] = os.environ.get("KEYSTORE", str(keys / "android-debug.keystore"))
    subprocess.run(["bash", str(android / "package.sh")], env=env, check=True)
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    apk = android / "build" / (title + "-arm64-debug.apk")
    shutil.copy2(apk, dist / apk.name)
    print(dist / apk.name)
else:
    print(stage)
