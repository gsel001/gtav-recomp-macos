# GTA V recomp — experimental macOS improvements

A downstream of [andrewnakas/gtav-recomp](https://github.com/andrewnakas/gtav-recomp),
with Mac presentation fixes, demand-prioritized shader compilation, reclaimable upload rings,
and optional frame/memory reports. The renderer remains Vulkan through MoltenVK.

See [the changes, build requirements and validation status](docs/MACOS_PERFORMANCE.md).
This repository contains source only; the new changes have not yet been validated in gameplay.
Upstream release links below point to upstream builds, which do not contain these changes.

---

Just want to play the game on Windows, Android, IOS, Mac or Linux? go here https://github.com/andrewnakas/gtav-recomp/releases

# GTA V (Xbox 360) recompiled: Windows, Linux, macOS, iPhone and Android

Grand Theft Auto V for Xbox 360 (Title Update 27), **statically recompiled** from the console's
PowerPC executable into native code, with a new renderer, for PCs, Macs and phones. Built with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) v0.10.0.

**No game is included, ever.** Not in this repository, not in any release. You bring your own
Xbox 360 discs and title update; everything made from them (the recompiled code, the game files,
the translated shaders) is made on your own computer and stays there.

> [!WARNING]
> **Forking, mirroring or publishing your own builds?** Read
> [Keeping it clean](#keeping-it-clean-rules-for-forks-and-downstream-releases) first. The
> recompiled game code, the game's shaders and anything else made from the discs are **Rockstar
> Games' copyrighted work**. They must never be in a repository, release, ipa, apk, mirror or
> download link. This project's GPL license covers only our own code and cannot give anyone the
> right to share Rockstar's.

**Downloads: [Releases](https://github.com/andrewnakas/gtav-recomp/releases).** Pick the package
for your computer (Windows, Linux or macOS). Every package can also build the **iPhone** and
**Android** versions, so you never need a separate phone download.

---

## Contents
- [How it works](#how-it-works)
- [What you need](#what-you-need)
- [Windows](#windows) · [Linux](#linux) · [macOS](#macos)
- [iPhone](#iphone) · [Android](#android)
- [Performance](#performance)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Repository layout](#repository-layout)
- [Keeping it clean: rules for forks and downstream releases](#keeping-it-clean-rules-for-forks-and-downstream-releases)
- [License and legal](#license-and-legal)

---

## How it works

1. **Setup copies and checks your game.** The launcher reads your Disc 1 and Disc 2 (ISO images or
   extracted folders) and your Title Update 27, copies the game files into its game folder, and
   checks every file against known-good SHA-256 hashes, so a damaged or wrong dump is caught
   up front.
2. **Your executable is recompiled on your machine.** The bundled ReXGlue codegen turns the game's
   PowerPC code into C++ (811 source files). Their hashes are checked against the reference build,
   so every player gets the same code. Then the bundled LLVM/clang compiles them into a native game
   library (`gtav_game.dll`, `.so` or `.dylib`). This takes 15-30 minutes once.
3. **The game runs on our runtime and renderer.** An Xbox 360 runtime (kernel, file system, audio)
   and a new Vulkan renderer (MoltenVK on Apple devices) that replays the game's draws natively.
   Game shaders are translated to SPIR-V the first time they are seen.
4. **Phones are built the same way, on your computer.** iOS and Android can't run a compiler or a
   shader translator, so your PC or Mac builds the phone's game library and shaders from your
   installed game ("Build for iPhone", "Build for Android"). The phone app then installs the game
   files itself from your discs.

What the downloads contain: our launcher and runtime, the renderer, the codegen tool, LLVM clang
and lld, the shader tools (XenosRecomp, dxc), phone app shells without any game code, and
reference hashes. Every release is taint-scanned against the game's files and the code generated
from them.

## What you need

- **Your own GTA V for Xbox 360**: Disc 1 (Install) and Disc 2 (Play), as ISO images or extracted
  folders, and **Title Update 27** (the `tu00000009_00000000` file from your console, or a folder
  with `disc002/default.xexp` and `disc002/update.rpf`). Other title updates won't work: the
  recompilation is made for TU27's executable.
- **A computer** (pick one):

  | | Windows | Linux | macOS |
  |---|---|---|---|
  | OS | Windows 10/11 x64 | x86-64 with glibc 2.35 + GCC 13 libstdc++ (Ubuntu 24.04, Debian 13, Fedora 39, Arch...) | macOS 15 or newer |
  | CPU | AVX2 (Intel Haswell / AMD Zen or newer) | AVX2 | Apple silicon (M1 or newer) |
  | GPU | Vulkan driver | Vulkan driver + loader | built in (Metal) |
  | Extra | none (the Visual C++ runtime is included) | a desktop session (X11 or Wayland) | none |

  8 GB RAM or more. About 25 GB free during setup, about 17 GB afterwards. A controller is
  recommended (Xbox or PlayStation through SDL; Windows: XInput). The keyboard works on Mac and Linux.
- **For the phones** (optional): see [iPhone](#iphone) and [Android](#android).

## Windows

1. Download `gtav-recomp-windows-x64.zip`, unzip it somewhere with enough space, and run
   `gtav_launcher.exe`.
2. Pick **Disc 1**, **Disc 2** and the **title update** (File... or Folder...), then press **Install**.
3. When setup finishes, press **Play**.

Later: running `gtav_launcher.exe` starts the game directly. Hold **Shift** while starting it to
get the setup window (redo setup, or **Build for iPhone** / **Build for Android**). Logs: `data\logs`.

## Linux

1. Download `gtav-recomp-linux-x64.tar.gz` and unpack it: `tar xf gtav-recomp-linux-x64.tar.gz`.
2. Open `gtav_launcher` (double-click it, or run `./gtav_launcher`). The setup window opens: choose
   Disc 1, Disc 2 and the title update, then press **Install**. (Without a desktop, setup runs in the
   terminal: `./gtav_launcher --setup "Disc 1.iso" "Disc 2.iso" tu00000009_00000000`.)
3. Press **Play**. Setup also adds "GTA V recomp" to your applications menu.

Later: the menu entry or `./gtav_launcher` starts the game. `./gtav_launcher --setup` (or the menu
entry's "Run setup again") opens the setup window with the phone buttons. Logs: `data/logs`.

## macOS

1. Download `gtav-recomp-macos-arm64.zip`, unzip it, and move `GTAV.app` to Applications.
2. The app is not notarized, so open it once, then go to System Settings > Privacy & Security >
   **Open Anyway** (or run `xattr -dr com.apple.quarantine /Applications/GTAV.app`).
3. Choose Disc 1, Disc 2, the title update and a game folder (default
   `~/Library/Application Support/GTAV Recomp`), then press **Install**, then **Play**.

Later: opening `GTAV.app` starts the game. Hold **Option** while opening it to get the setup window
with **Build for iPhone** / **Build for Android**. Esc in game opens the settings menu.

## iPhone

Needs an **iPhone 12 or newer** (A14 chip or later) on **iOS 17+**, about **17 GB** free, and a
sideloading tool that signs apps with your own Apple ID:
[Sideloadly](https://sideloadly.io) (Windows, macOS) or [AltStore](https://altstore.io).
A free Apple ID works, but the app has to be re-signed every 7 days.

1. **Install and play the game on your computer first, for a while.** The phone gets the shaders your
   computer has seen, so drive around and go inside some buildings.
2. **Build for iPhone.** Open the setup window (Windows: hold Shift starting the launcher; macOS:
   hold Option opening the app; Linux: `./gtav_launcher --setup`) and press **Build for iPhone**
   (or run the launcher with `--iphone`). It takes about 15-25 minutes and shows you
   `iphone/GTAV.ipa` in your game folder.
3. **Install the ipa** with Sideloadly or AltStore, with the iPhone connected.
4. **Put your discs on the phone.** Copy your Disc 1 ISO, Disc 2 ISO and the TU file
   (`tu00000009_00000000`) into GTA V's folder on the phone: Finder > your iPhone > Files > GTA V on
   a Mac, Apple Devices or iTunes > File Sharing on Windows, or the Files app. You can also keep
   them on a USB drive plugged into the phone.
5. **Open GTA V on the phone.** Its setup screen finds the files, copies and checks about 16 GB (a
   couple of minutes), offers to delete the disc images, and starts the game.

Controls: on-screen touch controls; a Bluetooth controller hides them. The game renders at 75% of
720p. **Missing objects?** Copy the phone's `GTA V/shader_cache` folder to `iphone/shader_cache` in
your game folder, press Build for iPhone again (a few minutes) and reinstall the ipa. Your game files
on the phone stay.

## Android

Needs a recent **Snapdragon** phone (tested: Galaxy S23 FE, Snapdragon 8 Gen 1) with about 40 GB
free during setup.

1. **Install and play the game on your computer first, for a while** (as for iPhone).
2. **Build for Android** in the setup window (or `--android`). You get
   `android/gtav-android-pack.zip` (your game code + shaders) and `android/GTAV.apk` (the app; no
   game inside).
3. **Copy both files and your Disc 1 ISO, Disc 2 ISO and TU file** to the phone (for example its
   Download folder).
4. **Install `GTAV.apk`** on the phone (allow installing apps from that source).
5. **Open GTA V**, choose the pack and the three disc files, and press **Install**. It copies and
   checks about 16 GB, then starts. You can delete the disc images and the pack afterwards.

**Missing objects?** Copy the phone's `Android/data/com.gtavrecomp.app/files/shader_cache` folder to
`android/shader_cache` in your game folder, press Build for Android again, and choose the new pack on
the phone.

## Performance

Rough numbers measured during development; your hardware will differ.

| Device | Typical |
|---|---|
| Apple M1 Mac (8 GB) | ~45-60 fps in the open world |
| iPhone 13 mini | ~30 fps in the city and the prologue, ~20 fps in the heaviest scenes; slower when the phone is hot |
| Galaxy S23 FE | ~28-29 fps cool, 14-17 fps when thermally throttled (on the charger) |

Phones get faster after the first session: compiled pipelines are cached, so a new effect stutters
only the first time it appears. Phones throttle when hot: play unplugged, or out of a case.

## Troubleshooting

- **Setup says files differ from a known good dump**: your ISO or TU is damaged or modified.
  Re-dump it. The title update must be TU27.
- **Missing objects (holes, invisible things) right after starting**: shaders are being translated.
  On a computer this fixes itself within minutes. On a phone, use the shader_cache round trip above.
- **iPhone app expired**: re-sign it with Sideloadly or AltStore (free Apple IDs: every 7 days). Your
  game files stay.
- **iPhone app closes on its own**: the phone ran out of memory. Close other apps, and restart the
  phone if you've been using it heavily. The game needs about 1.5-1.7 GB.
- **Logs**: `data/logs` in the game folder (`gtav.log`, `install.log`, `iphone_unattended.txt`...),
  and on the phone in the app's `logs` folder. Please attach them when you report a problem.

## Building from source

The release is built from this repository plus three upstream trees with our patches:
`rexglue-sdk-gtav-linux.patch` (ReXGlue SDK v0.10.0, all platforms), `xenosrecomp-gtav.patch`
(XenosRecomp), `plume-gtav.patch` (plume). Each release attaches a source archive with everything at
the exact commits used. Platform notes:

- Release process, clean-room rules and taint scanning: [`docs/RELEASE_CLEANROOM.md`](docs/RELEASE_CLEANROOM.md)
- iPhone port (build, deploy, performance, release layout): [`docs/IOS_PORT.md`](docs/IOS_PORT.md)
- Android port: [`docs/ANDROID_STATUS.md`](docs/ANDROID_STATUS.md)
- Phone builds from any PC: [`docs/HANDOFF_ANDROID_PC_BUILD.md`](docs/HANDOFF_ANDROID_PC_BUILD.md),
  [`docs/HANDOFF_WINDOWS_PHONES.md`](docs/HANDOFF_WINDOWS_PHONES.md)
- ARM64 / Apple notes: [`docs/ARM64_NOTES_FOR_ANDROID.md`](docs/ARM64_NOTES_FOR_ANDROID.md)

Release scripts: `tools/release/build_release_macos.sh` (+ `IOS_SHELL=1` for the iPhone shell),
`tools/release/make_release.py` (Windows), `tools/release/make_release_linux.py`; phone shells:
`tools/ios/make_shell.sh`, `tools/android/make_shell.sh`.

## Repository layout

- `gtav/`: the game project: CMake, manifest of entry points, `src/` hooks, fixes and the native
  renderer (`src/native/`), iPhone setup screen (`src/ios_setup.mm`), Android shim and setup
- `gtav/fs/`: freestanding headers the game library is compiled with (no OS or compiler SDK headers)
- `loader/`: the launcher and installer (`install.cpp` is shared by all platforms, including the
  phone builds), `main.cpp` (Windows), `main_linux.cpp` (GTK/terminal), `main_mac.mm` (AppKit)
- `tools/game_dll/`: interface generation, the reference hashes' sources, dev builds of the game library
- `tools/ios/`, `tools/android/`: phone builds, shells, shader tools, deploy scripts
- `tools/release/`: release packaging, taint scanning, READMEs shipped in the packages
- `scripts/`: manifest derivation, XEX / STFS / ISO extraction
- `docs/`: status, handoffs and process documents

## Keeping it clean: rules for forks and downstream releases

This project only works because it **never distributes Rockstar Games' or Microsoft's work**. The
downloads contain our launcher, runtime, renderer and tools, and nothing from the game. Every player
recompiles their own copy from their own discs. If you fork this repository, mirror the releases,
package it for a store or distro, or share builds with friends, the same rules apply to you.

### Never distribute any of these

| What | Why it is Rockstar's / Microsoft's |
|---|---|
| Disc images (ISO), extracted disc folders, `default.xex` / `default.xexp`, `.rpf` archives, install packages | the game itself |
| Title updates (`tu00000009_00000000`, `update.rpf`), or links to sites that host them | the game's executable and data |
| Extracted assets: models, textures, audio, maps, scripts, text | the game's data |
| **The generated C++** (the 811 files the codegen writes) | a mechanical translation of the game's code is still the game's code |
| **Compiled game libraries**: `gtav_game.dll`, `gtav_game.so`, `gtav_game.dylib`, `libgtav_game.so`, or any exe or app with the game linked in | compiled game code |
| **Game shaders in any form**: the originals, HLSL, SPIR-V (`data/spirv`), Metal, pipeline caches, a phone's `shader_cache` | translated game code |
| **A `GTAV.ipa` or Android pack made by Build for iPhone / Android** | they contain your compiled game library and shaders |
| Your game folder (`data/`), saves, memory dumps, GPU captures, traces, and logs that contain game memory | game data |

Only the "empty" shells may be shared: the release packages, the iPhone shell (`ios/`) and the
Android APK as they ship. They contain no game code, and they are useless without a game the player
builds from their own discs.

### Rules for working on the code

- **Never use the leaked Rockstar source code**, not even for names, comments or structure. Work only
  from your own analysis of your own copy of the game.
- Don't commit anything game-derived, even temporarily. Git history is permanent, and a deleted
  file stays downloadable from old commits.
- Function addresses, sizes and hook names (our manifest, the codegen partition, `expected.txt`
  hashes) are our own analysis and are fine. Code copied from the game is not, including code
  pasted from a disassembly or from the generated C++.

### Before you publish anything

1. Build the release from a clean checkout with the release scripts in `tools/release/`.
2. Run the taint scan on the finished package: `tools/release/scan_release.sh` (Linux),
   `tools/release/scan_release_macos.sh` (macOS) or `tools\release\scan_release.cmd` (Windows).
   It checks the package byte for byte against the generated code, a compiled game library and
   the game's shaders, and it must pass. Explain any match it reports, as our release notes do.
3. Unpack your own download and look at what is in it before you share the link.

The process is described in [`docs/RELEASE_CLEANROOM.md`](docs/RELEASE_CLEANROOM.md).

### Why this matters

- **The GPL can't help you here.** The GNU GPL covers *our* code, and you may share and modify that
  freely under its terms. It gives no rights to Rockstar's or Microsoft's work, because nobody here
  owns them. Sharing a build with the game inside is copyright infringement by whoever shares it,
  whatever license file sits next to it.
- **Recompiling isn't a clean room.** The output of the recompiler is the game's code in another
  form. It is legal for you to make it from a copy you own. It isn't legal to hand it to someone else.
- **One bad mirror can end the project for everyone.** A takedown aimed at a build with game content
  can take this repository with it.

If you find game content in this repository, in a release, or in a fork that links here, please open
an issue (without attaching the content) so it can be removed.

## License and legal

This project is free software under the **GNU GPL, version 3 or later** (`LICENSE`). Bundled
third-party components keep their own licenses; every package carries them in `LICENSES/`, and each
release attaches the complete corresponding source.

No Rockstar Games or Microsoft code, data, shaders or title-update files are in this repository or
in any release, and the project never used leaked source code. What you make from your own discs is
for you alone: don't share your game folder, your `GTAV.ipa` or your Android pack (see
[Keeping it clean](#keeping-it-clean-rules-for-forks-and-downstream-releases)). The GPL applies
to this project's own code only; it grants no rights to Rockstar Games' or Microsoft's work.

Grand Theft Auto V is a trademark of Take-Two Interactive Software. This project is not affiliated
with or endorsed by Rockstar Games, Take-Two Interactive, Microsoft, Apple or Google.
