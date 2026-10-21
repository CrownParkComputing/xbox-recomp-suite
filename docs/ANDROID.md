# Android runtime and device recipe

The shared launcher supports a local `.iso` through `ACTION_OPEN_DOCUMENT` or
an extracted folder through `ACTION_OPEN_DOCUMENT_TREE`. Most titles expect
`default.xbe`; HOTD3 expects `hod3xb.xbe` and OutRun 2 expects `outrun2.xbe`.
HOTD3 and OutRun 2 use folders because their supplied arcade media is not a
standard Xbox ISO. The Virtua Cop 3 build accepts the matching MYSTRO Xbox ISO
or its extracted folder.
Only a persistent read grant is retained. Files stay on the selected SD card.
The APK contains no game data.

After the first valid selection, boot goes directly into the game. If the file,
folder, card, or grant becomes unavailable, the picker appears again. A selected
unavailable path never silently falls back to an older private copy. For an
intentional change, open the debug picker:

```sh
adb shell am start -n com.splashdown.recomp/.LauncherActivity --ez show_picker true
```

The native game Activity and session service run in `:game`. Minimizing or
closing ends that process; reopening creates fresh guest state, GPU/audio state,
and file descriptors using the remembered location. The small launcher process
may remain cached by Android. A cached launcher is not retained native game RAM.

## Controls

Connect a physical controller before starting. Sticks, D-pad, face buttons and
analog triggers use SDL's mappings; R1 is Xbox Black and L1 is White. Select is
Back. R3 opens settings and automatically pauses the game, its clocks and audio.
R3 closes settings and resumes; the original Start/pause state is preserved.
This applies to all ten ports, including BLACK's custom settings panel.
On Linux, R3 or F10 opens the same panel.
The original seven retail Xbox ports require a physical controller.

## Chihiro touch controls

| Port | Touch controls |
| --- | --- |
| Virtua Cop 3 | Tap/drag the game area to aim and fire; Start, Reload and Slow buttons; Settings pauses the game |
| HOTD3 | Tap/drag the game area to aim and fire; Reload pumps the shotgun; Coin then Start; Settings pauses the game |
| OutRun 2 | Hold Left/Right to steer; Gas accelerates and confirms selections; Brake slows; Coin then Start; Settings pauses the game |

The controls accept multiple fingers, so steering and acceleration can be held
together. Gun aim uses the renderer's centered 4:3 presentation rectangle;
tapping outside it reloads. A second aim finger cannot steal the primary aim.
Focus loss and touch cancellation release held controls. A USB/Bluetooth mouse
can also use the shooters' desktop left-fire/right-reload mappings.

The ARM64 APKs build and their native mouse/touch tests pass. No Android device
was connected for this addition, so these three games' on-device boot, graphics,
touch alignment, performance and sound remain unverified. Earlier device results
below apply to the original seven titles.

## Device checks

1. Select an SD image/folder once, boot to the title, and verify D: reads it.
2. Press Home, wait for `onStop`/destruction, and confirm the `:game` PID exits.
3. Reopen and confirm a different native PID, no picker, and intact saves.
4. Make the selected path temporarily unavailable, confirm the picker returns,
   then restore the path and confirm direct boot returns.
5. While driving, press R3: verify the scene and race timer stay fixed while
   the panel responds to navigation. Close with R3 and verify smooth resumption.
   Repeat while the original game is already paused and during an early intro.
6. Test a real race/gameplay scene, controller navigation, music/effects,
   graphics, and save/reload separately. Intros alone do not validate gameplay.

The connected Retroid Pocket Flip 2 (Android 13, Adreno 650, Vulkan 1.1.128)
passed SD boot, remembered selection, and fresh-session checks across the original
six ports (Splashdown, Burnout 3, Links 2004, NFS Hot Pursuit 2, PGR1, and BLACK).
L.A. Rush subsequently passed SD folder boot, intro/title playback and a
Home/reopen cycle: its native PID exited and a different PID booted directly
from the remembered folder. All 2,132 SD game files matched their local SHA-256
hashes. Its later XACT locking fix also passed basic Story free roam, steering,
reverse and collision checks; full races and mission completion remain open.
NFS, Burnout and Links' old private data was removed only after every SD
file hash matched and boot from SD succeeded; saves/settings were retained.
Those local user data/checksum files are deliberately absent from this repository.

## Driver and renderer lessons

Older Adreno devices lack dynamic rendering and expose promoted Vulkan APIs
under extension names. Use the SDK's dispatch table and legacy render passes.
BLACK keeps its custom renderer with a staged ARM64 byte comparison, format/MSAA
matched render passes, per-frame framebuffer caches and extension dispatch.
When recreating its swapchain, drop only swapchain framebuffers: an offscreen
framebuffer may still be referenced by commands being recorded for that frame.
Final blits and ImGui draw data obey the swapchain's display pre-rotation.

Splashdown's 4531 state indices differ from later XDKs. The isolated overlay maps
those indices, preserves vertex point size, and generates stage-3 sprite UVs;
this restores jet-ski spray. Its music, engine and water sources produce mixed
PCM, but DSP/reverb and velocity Doppler fidelity remain incomplete.

L.A. Rush's first presentation initially crashed in Adreno's
`vkEndCommandBuffer`: swapchain creation deleted offscreen framebuffers already
referenced by recorded guest clears. The shared SDK now marks
swapchain-backed framebuffers and drops only those when swapchain views expire.
The corrected device build retained the three offscreen framebuffers and
completed intro/title playback. BLACK's custom renderer already applies the
same lifetime rule.

The shared audio mixer retains mix-in bus routing, so PGR's opponent engines
inherit volume and 3D placement. Its XDK 3925 sound-mode wrapper is identified
separately from maximum distance. Final stereo peak limiting prevents overloaded
voices from hard clipping; DSP/reverb fidelity still requires comparison by ear.
XACT's DISPATCH_LEVEL regions are also serialized across host threads: merely
updating the emulated IRQL byte allowed L.A. Rush's event queue to race.

Logs are private `files/logs` and use each title's tag in logcat. Examples:

```sh
adb shell run-as com.splashdown.recomp ls files/logs
adb logcat -v time Splashdown:V SDL:V AndroidRuntime:E libc:F '*:S'
```

## Foreground startup and settings pause

The task-removal service starts after the game window gains focus. Starting it
in `onCreate` can precede Android's foreground UID transition and throw
`BackgroundServiceStartNotAllowedException`. The optional guard catches that
condition; native process cleanup remains in `onStop`/`onDestroy`.

R3 is reserved before Android's per-device SDL mapping. This also handles pads
which report stick clicks with a keyboard source. Key repeats do not reopen the
panel, and the closing press does not reach the guest. Linux uses the SDL R3
button event. An early press defers suspension until the renderer can draw its
first frame, avoiding a boot deadlock.

The renderer redraws the retained frame while guest workers stop at safe API
boundaries. Threads finish elevated IRQL regions before parking. Guest time,
relative waits, timer deadlines, vertical blanks and PCM playback exclude menu
time. Host UI redraws do not advance guest GPU frame/fence counters. Render-size
changes apply after resuming so the retained frame survives settings edits.
