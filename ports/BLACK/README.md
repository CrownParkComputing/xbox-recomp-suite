# Black - XBE Recomp
An unofficial native PC port of the original Xbox version of Black, with extended graphics options, keyboard and mouse support, and other enhancements.

> Source-suite snapshot: use [the shared build guide](../../docs/BUILDING.md)
> and [Android instructions](../../docs/ANDROID.md). The notes below retain the
> original port context; any standalone Android paths are superseded by the
> shared suite commands.

This port was created using the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

> [!NOTE]
> This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build.

## Installation
- Put your game files in the `assets` folder. This can be an ISO, or the extracted data files, of the USA or Europe release.
- Run `BLACK.exe`.
- Recomp settings can be adjusted in the in-game options menu while the game is running, or through the Settings overlay, which is opened with F10 by default.
<img width="1920" height="1080" alt="{5852D359-24A0-48C8-9319-07C3A619E355}" src="https://github.com/user-attachments/assets/e0156eb8-c1b5-49d9-ab93-3abb46bf5f47" />


## Features
### Display
- **Monitor** - Picks the monitor the game shows on, for Fullscreen and Borderless (and where Windowed opens). Lists every connected monitor by its Windows number and desktop resolution.
- **Display Mode** - Switch between Fullscreen, Borderless and Windowed.
- **Resolution** - Lists every mode your monitor reports and defaults to your desktop resolution, replacing the original 640x480 resolution.
- **Render Resolution** - Renders the game at 20% to 200% of the output resolution, allowing supersampling or downscaling independent of the window size.
- **Field of View** - Sets the horizontal field of view during gameplay from 45 to 140 degrees, with 70 being the game's default.
- **Viewmodel Field of View** - Adjusts the first-person weapon and arms field of view.
- **Max Frame Rate** - Caps the frame rate to your monitor's refresh rate or a custom value, or removes the cap entirely, replacing the Xbox's 30 FPS limiter.
- **V-Sync** - Reduces screen tearing.
- **HDR Output** - Enables HDR10 output on displays with Windows HDR turned on, detected automatically. Highlights extend up to the display's peak brightness.
- **FPS Counter** - Displays the frame rate on screen.
- **FMV Scale** - Choose between Fill, which zooms the 4:3 FMVs to fill widescreen displays, and 4:3, which keeps them at their original aspect ratio.

### Graphics
- **Multisample Antialiasing (MSAA)** - Enables 2x, 4x or 8x MSAA to smooth jagged edges while preserving a sharp image.
- **Subpixel Morphological Antialiasing (SMAA)** - Enables SMAA to smooth jagged edges across the whole frame with a softer image. SMAA also smooths alpha-tested edges such as fences and foliage, and can be combined with MSAA.
- **Anisotropic Filtering** - Forces the selected anisotropic filtering level on all textures.
- **Draw Distance** - Increases the level of detail and draw distances past the game's values: 2x, 4x or 8x.
- **Ambient Occlusion** - Adds screen space ambient occlusion, creating soft contact shadows where surfaces meet. Choose between SSAO and GTAO.
- **Reload Blur** - Toggles the blur effect while reloading.

### Input
- **Mouse and Keyboard** - Full keyboard and mouse support with raw input, adjustable sensitivity, inverted look and rebindable controls.
- **Controller** - Controller with SDL3 support. 

### General
- **Skip Intro** - Skips the intro logos.
- **Pause On Focus Loss** - Pauses the game and its sound while the window is in the background or minimized.

### Miscellaneous
- **Custom Menu Cursor** - Place a `cursor.png` (or `cursor.dds`) beside the exe to replace the Windows arrow in the game's menus.

## Linux
A native Linux build renders through Vulkan, plays sound through SDL3 (PipeWire, PulseAudio or ALSA) and runs its window on Wayland or X11. Every feature above works, HDR10 included where the compositor offers it.

- Put the game in the `assets` folder: the extracted files with `default.xbe`, or a disc image (`.iso`), which `build.sh` extracts there.
- Run `./build.sh`. It checks the toolchain (clang, lld, cmake, ninja, SDL3, the Vulkan loader), recompiles the game, builds it and runs the tests.
- Run `out/build/linux-release/BLACK`. Keep `libdxcompiler.so` beside it: the renderer compiles its shaders with it.
- HDR10 brightness: `XBE_HDR_PAPER_NITS` and `XBE_HDR_PEAK_NITS` set SDR white and the display's peak (defaults 200 and 1000).
- Not on Linux: the boot splash, and `cursor.dds` (use `cursor.png`). Fullscreen on Wayland is scaled by the compositor rather than a mode switch.

See `docs/linux_port.md` for how the port maps onto Windows' D3D12 and Win32 code.

## System Requirements
### Minimum
- **OS**
  - Windows 10 64-bit
- **CPU** with 2 cores and support for SSE4.2 and POPCNT
  - Intel Nehalem (Intel Core 1st Generation)
  - AMD Bulldozer (AMD FX series)
- **GPU** with support for Direct3D 12
  - NVIDIA GeForce GT 630 (Kepler)
  - AMD Radeon HD 7750
  - Intel HD Graphics 510 (Skylake)
- **Memory**
  - 4 GB
- **Storage**
  - 2 GB for the game files and saves

## Building
### 1. Clone the Repository
Clone this repository using [Git](https://git-scm.com/).
```
git clone https://github.com/TGP482/BLACK-XBERecomp.git
```

### 2. Add the Required Game Files
Place your extracted game data files in the `assets` folder. `default.xbe` from the USA release must be there, with the rest of the game's files beside it.


### 3. Install Dependencies
Install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) and select the following **Workloads** and **Individual components**:
- Desktop development with C++
- C++ Clang tools for Windows
- C++ CMake tools for Windows

> [!TIP]
> `build.bat` offers to install the Clang and CMake components for you if they're missing.

### 4. Build the Project
- Run `build.bat`. The first build recompiles the entire game and can take 30 to 90 minutes on weaker systems.
- When it finishes, run `out\build\release\BLACK.exe`.
