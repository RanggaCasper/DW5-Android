# Dynasty Warriors 5 for Android

An unofficial Android (arm64) port of the original Xbox version of *Dynasty Warriors 5*, made with the
[XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) SDK. The game's x86 code in `default.xbe` is statically
recompiled to C++ on a PC, then built for Android together with the SDK's portable runtime and this repo's host code.
Drawing goes through Vulkan, and windowing, input and audio through SDL3. No emulator is involved.

> [!NOTE]
> This project does not include any game assets. You must provide the files from your own legally acquired copy of the
> game to install or build.

## Status

Work in progress. Brought up from the Linux port ([DW5-XBERecomp](https://github.com/datfooldive/DW5-XBERecomp-Linux)).

## Installation

Android 10 (API 29) or newer, arm64, with a Vulkan 1.1 driver.

1. Install the APK.
2. Open **DW5 Android** and allow **all files access** when it asks (on Android 10, the **storage** permission).
3. Copy the game into `/storage/emulated/0/DW5`, or into a `DW5` folder at the top of your SD card: the USA release,
   either as a disc image (`.iso`) or as `default.xbe` with its data files beside it. The app creates the folder once
   access is allowed, and starts the game as soon as it finds the files. A copy in the app's own
   `Android/data/com.datfooldive.dw5/files/assets` works too.

The saves (`hdd`) live in the game's folder, beside its files, so uninstalling the app does not take the progress
with it. `settings.toml` stays in the app's own files folder. There is no log file on the device: the release writes
its Report and Error lines to stderr, which `adb logcat` carries.

## Controls

The controls sit where the original pad has them:

| | |
| --- | --- |
| left stick | walk, as the pad's |
| drag on the right | turn the camera, easing out when the finger stops |
| **X Y B A** | attack, charge, musou, jump, in the pad's diamond (X left, Y up, B right, A down) |
| **D-pad** | the menus, four buttons apart so a thumb on one does not touch the next |
| **START** / **BACK** | pause, map |
| **LT** / **RT** | the triggers |
| **L3** / **R3** | the stick clicks, under each thumb; both at once for a rage |
| **gear** | the settings panel, top middle |

The buttons are drawn with the pad's own colours, from `native/ui/btn_*.rgba` (RGBA8888, 96 x 96, embedded by CMake:
the Android build has no image decoder). Their layout follows the Duke pad drawing xemu ships, `data/duke.svg`; that
file is a colour-coded mask for xemu's own UI, so the glyphs here are drawn to match it rather than copied from it.

A controller works as on the Xbox, with its rumble; while one is plugged in the on-screen controls are hidden, the
gear staying for the panel. A keyboard works as port 1's pad, under a real pad if one is plugged in. The panel takes the D-pad and **A** as well as a finger, so it can
be worked without letting go of the pad.

## Settings

The gear opens a panel over the game, which keeps running behind it and reads no touch until it closes. It is laid
out as a row per setting: the label left, the value between two arrows right, sections over them, and Back and
Defaults at the foot. A tap on an arrow, the arrow keys or the pad's D-pad steps a value, which is applied and
written to `settings.toml` at once.

| row | | default |
| --- | --- | --- |
| **Render resolution** | 20 to 200% of the output height in steps of 5, with the pixels it makes | 67% |
| **Anti-aliasing** | OFF, SMAA over the finished frame, or MSAA 2X / 4X on the scene | OFF |
| **Anisotropic filtering** | the game's own, or 2X to 16X over it | GAME DEFAULT |
| **Vertical sync** | FIFO, or immediate / mailbox tearing without it | ON |
| **Frame skip** | AUTO: a frame left undrawn while the drawing is behind, the game still at its speed | AUTO |
| **Aspect ratio** | the TV's shape, 16:9, or the whole screen | ORIGINAL |
| **FPS counter** | the frame rate in the top left corner, amber under the cap | ON |
| **HDR** | HDR10 when the display offers it; greyed when it does not | OFF |

The frame rate cap (60) and the display mode are only in the file. 30 is not a cap the port offers: the title counts
its timing at 60 and runs at half speed below it.

## Building

Everything runs on Linux. Requirements: clang, cmake, ninja, git, the Android SDK with NDK `30.0.16138531` and CMake
`3.22.1`, and Java 17 or newer. `build.sh` writes `local.properties` from `ANDROID_HOME` (default `~/Android/Sdk`).

1. Put the game in `assets/`: `default.xbe`, or the USA disc image (`.iso`).
2. Run `./build.sh`. It builds the recompiler, recompiles `default.xbe` into `generated/game`, then builds
   `app/build/outputs/apk/release/app-release.apk` with Gradle.
3. Install: `adb install -r app/build/outputs/apk/release/app-release.apk`.

The first native build compiles 64 partitions of recompiled code for arm64 and takes 30 to 90 minutes.

## Layout

| path | |
| --- | --- |
| `native/` | the Android host: `main.cpp` (SDL window, event loop, lifecycle), `vulkan.cpp` (the Vulkan backend), `vk_shader.cpp` (HLSL to SPIR-V with glslang), `touch.cpp` (the on-screen controls), `menu.cpp` (the settings panel), `game_files.cpp`, `system_info.cpp`, `ui/*.rgba` |
| `src/` | the game's host code, shared with the Linux port: hooks (the draw distance among them), the D3D12 recording on the game thread and its replay on a render thread of its own, SMAA, audio, keyboard, options |
| `sdk/` | XboxXBE-Recomp with this port's runtime fixes |
| `config/game.toml` | recompiler input: sections, entry point, XDK symbol map, `[abi]` |
| `app/` | the Gradle module: `DW5Activity`, the storage permission prompts, the icon |
| `tools/host/` | the recompiler as a PC tool, built by `build.sh` |

## Credits

- [TGP482](https://github.com/TGP482) for [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler
  and runtime SDK this port is built on.
- The Vulkan renderer came from the BLACK forks, [datfooldive/BLACK-XBERecomp-Linux](https://github.com/datfooldive/BLACK-XBERecomp-Linux)
  and [datfooldive/Black-Android](https://github.com/datfooldive/Black-Android).
- [SDL](https://github.com/libsdl-org/SDL), [glslang](https://github.com/KhronosGroup/glslang),
  [toml++](https://github.com/marzer/tomlplusplus), [Dear ImGui](https://github.com/ocornut/imgui) and
  [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).

Dynasty Warriors 5 is © 2005 Koei Co., Ltd. This project is not affiliated with or endorsed by Koei.
