# Isolated SDK changes

This is the historical seven-patch series from the original working checkout.
In the source suite, use `tools/prepare.py`; its single overlay already includes
these changes. See [the current build guide](../../../docs/BUILDING.md).

`sdk` links to `.local/sdk`; the shared `/home/jon/XboxXBE-Recomp` checkout is
unchanged. To reproduce this port from that baseline, apply these patches in order:

1. `4531-reset-presentation.patch`
2. `4531-audio-voices.patch`
3. `4531-race-rendering.patch`
4. `4531-android-host.patch`
5. `4531-sd-documents.patch`
6. `controller-ports.patch`
7. `4531-point-sprites.patch`

Run `git -C <sdk> apply <absolute-patch-path>` for each, then recompile the game
and rebuild the native targets. All seven patches were checked sequentially
against the original SDK and reproduce the changed working SDK files.

The point-sprite patch translates 4531's older render-state indices, carries
vertex point size through the shader, and generates texture-stage-3 coordinates
for correctly sized particle quads. `tools/render_states_smoke.cpp` tests actual
4531 API and inline state writes; SDK render tests check textured point sprites
with fixed and programmed size at 1x and 2x scale.

The sibling Android staging workflow shares only the generic document, logging,
and controller fixes. It does not apply Splashdown's 4531 graphics/audio changes
to other titles. BLACK retains its custom renderer; `android/ports/black_vulkan.py`
stages its Vulkan extension dispatch and render-pass compatibility changes.
