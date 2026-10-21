# Shared Android sources

The ten ports share the SDL Java integration, remembered ISO/folder picker,
dedicated native process lifecycle, document-tree JNI reader, and glslang shader
adapter in this directory. Build with `python3 tools/build_android.py <title>`
from the repository root; do not invoke the old standalone packaging commands.

- [Build prerequisites and commands](../docs/BUILDING.md)
- [Controls, SD data, lifecycle and device checks](../docs/ANDROID.md)
- [Porting cookbook](../docs/XBE_PORTING_COOKBOOK.md)

No game data, generated guest code, native libraries, APKs, or signing keys are
included. The build produces those locally in ignored directories.
