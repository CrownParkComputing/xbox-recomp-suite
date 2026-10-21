# Build and run on Linux or Android

These are experimental native ports. Check [VERIFICATION.md](VERIFICATION.md)
for what has actually been tested. The repository includes the runtime,
recompiler, SDL and other dependency sources, Android launcher, and cookbook.
You supply the game files. It contains no ISO, retail XBE, generated game code,
APK, saves, or signing keys.

## Get the source and tools

```sh
git clone https://github.com/CrownParkComputing/xbox-recomp-suite.git
cd xbox-recomp-suite
```

Install the Linux prerequisites in [BUILDING.md](BUILDING.md) before running
the build scripts. Android builds also use Linux to recompile the Xbox code,
then cross-compile it with the Android NDK. The tested Android tools are API 35,
build tools 36.0.0, NDK 28.2.13676358, and JDK 17; Gradle is not required.

All commands below run from the suite root. Title names are case-sensitive:

| Game | Build title / asset directory | Linux executable | Android package |
| --- | --- | --- | --- |
| Splashdown | `Splashdown` | `Splashdown` | `com.splashdown.recomp` |
| Burnout 3 | `Burnout3` | `Burnout3` | `com.burnout3.recomp` |
| Links 2004 | `Links2004` | `Links2004` | `com.links2004.recomp` |
| NFS Hot Pursuit 2 | `NFSHP2` | `NFSHP2` | `com.nfshp2.recomp` |
| Project Gotham Racing | `PGR1` | `PGR1` | `com.pgr1.recomp` |
| BLACK | `BLACK` | `BLACK` | `com.black.recomp` |
| L.A. Rush | `LARush` | `LARush` | `com.larush.recomp` |
| Virtua Cop 3 (MYSTRO) | `VCop3` | `VCop3` | `com.vcop3.recomp` |
| The House of the Dead III | `HOTD3` | `HOTD3` | `com.hotd3.recomp` |
| OutRun 2 (Chihiro 2.000) | `OutRun2` | `OutRun2` | `com.outrun2.recomp` |

Each APK is `dist/<title>-arm64-debug.apk`. Each Linux executable is under
`ports/<title>/out/build/linux-release/`. The scripts prepare the appropriate
shared SDK or title-specific overlay automatically.

## Supply the matching game

For L.A. Rush, put your extracted game in `ports/LARush/assets`, with
`ports/LARush/assets/default.xbe` and all its original folders together. An
absolute symlink to your existing extracted folder also works:

```sh
ln -s /absolute/path/to/your/extracted/LA-Rush ports/LARush/assets
```

Use that command only if `ports/LARush/assets` does not already exist. Keep the
game's directory layout intact. The port's configuration and hooks are specific
to its executable version; another release may require porting changes. The
runtime checks the selected XBE against the one used to build the application.
L.A. Rush's validated XBE hash is listed in [BUILDING.md](BUILDING.md#la-rush).

If you have an ISO, first build the extraction tools without game files:

```sh
python3 tools/build_linux.py LARush --tools-only --jobs 4
ports/LARush/out/build/linux-release/iso_extract --all "/path/to/LA Rush.iso" ports/LARush/assets
```

The recompiler needs the matching executable as a local extracted file, even if you intend
to select an ISO on Android. Game inputs and generated outputs are ignored by
Git; do not force-add them.

## Linux desktop

```sh
python3 tools/build_linux.py LARush --jobs 4
./ports/LARush/out/build/linux-release/LARush
```

Run from a normal graphical Linux session with access to its Wayland or X11
display and a working Vulkan driver. Keep the executable in the build directory:
it searches parent directories for `assets`. If moving a build elsewhere, put
the matching `assets` folder alongside the executable.

Connect a physical controller. SDL maps the sticks, D-pad, face buttons, and
triggers; R1 maps to Xbox Black, L1 to White, and Select to Back. There is no
touch overlay for the seven retail Xbox ports. The three Chihiro ports have
touch controls described in [ANDROID.md](ANDROID.md#chihiro-touch-controls).
R3 opens settings and automatically pauses gameplay,
game clocks and audio. R3 closes settings and resumes. F10 also works on Linux.
BLACK retains its custom panel and follows the same pause contract.

The desktop runtime writes saves/cache to `hdd/`, logs to `logs/`, and settings
beside the executable. Back up those folders before replacing or cleaning a
build. Closing the application ends the native session.

## Android build and install

Use an ARM64 device running Android 13/API 33 or newer. Set the SDK and Java
paths to match your machine; these examples use the verified tool versions:

```sh
export ANDROID_SDK_ROOT=/opt/android-sdk
export NDK="$ANDROID_SDK_ROOT/ndk/28.2.13676358"
export JAVA_HOME=/path/to/jdk-17
export BUILD_TOOLS=36.0.0
```

Generate guest C++ and build the APK:

```sh
python3 tools/build_linux.py LARush --generate-only --jobs 4
python3 tools/build_android.py LARush --jobs 4
adb devices
adb install -r dist/LARush-arm64-debug.apk
adb shell am start -n com.larush.recomp/.LauncherActivity
```

The first command can be skipped if you already built the same game's Linux
target from the same XBE. Android packaging includes native code and launcher
resources; game files remain external.

On the device, connect your controller, then choose the ISO or extracted folder
on your SD card. For a folder, select the directory containing `default.xbe`.
The launcher retains the document permission and goes directly into the game
on later launches. It shows the picker again if that location or permission
becomes unavailable. Minimizing or closing ends the dedicated game process;
reopening starts a fresh session using the remembered location.

To install another title, change the build title, APK filename, and package
using the table above. For example:

```sh
python3 tools/build_linux.py BLACK --generate-only --jobs 4
python3 tools/build_android.py BLACK --jobs 4
adb install -r dist/BLACK-arm64-debug.apk
adb shell am start -n com.black.recomp/.LauncherActivity
```

The first Android build creates a private debug signing key at
`.local/keys/android-debug.keystore`. Keep it for future updates. If updating
an existing installation, set `KEYSTORE` to its original debug key before
building, then use `adb install -r`. A different signature cannot update the
existing application; uninstalling deletes its private saves/settings.

Saves, settings and logs live in each app's private storage, separate from the
SD game files. For a debug build, inspect or back up that storage with `adb`:

```sh
adb shell run-as com.larush.recomp ls files
adb shell run-as com.larush.recomp ls files/logs
adb exec-out run-as com.larush.recomp tar -cf - files > "$HOME/LARush-private-backup.tar"
```

Keep backups outside the source repository. See [ANDROID.md](ANDROID.md) for
the lifecycle checks and detailed device notes.

## Troubleshooting

- **Missing `assets/default.xbe`:** extract the game into the selected title's
  asset directory before generating or building game code.
- **Missing `generated/game/x86_func_mapping.cpp`:** run the Linux build script
  with `--generate-only` for that title before the Android build.
- **Different game release:** select the same game/XBE used for the build.
  Existing title hooks are not automatically compatible with every release.
- **SDL reports no video device:** launch inside your graphical desktop session
  with display access. A restricted shell or remote session may not have access
  to the display socket.
- **Vulkan initialization fails:** check your installed Vulkan driver and the
  application's log. CPU rendering used for diagnostics does not establish
  playable performance on a phone or handheld.
- **Picker reappears:** verify that the SD card is mounted and the selected file
  or folder is still present and accessible. To deliberately choose another
  location, use `adb shell am start -n com.larush.recomp/.LauncherActivity --ez show_picker true`.
- **Black screen or incomplete gameplay:** save the log and check the port's
  documented status. Intro playback alone does not establish gameplay support.

The original six ports have device evidence listed in [VERIFICATION.md](VERIFICATION.md).
L.A. Rush has desktop and Android Story driving, SD boot and fresh-process
relaunch evidence. Full missions and race completion remain unvalidated.
