# TwinStudio

TwinStudio is a set of modding and reverse-engineering tools for Crash Twinsanity's PS2 file formats. It currently ships two standalone editors, plus a shared library of format readers/writers.

| Tool | Status | Description |
| --- | --- | --- |
| [`twinmusic`](src/music_manager) | Usable | Editor for the game's `.MH`/`.MB` music and voice-over archives |
| [`twinpss`](src/pss_editor) | Usable | Editor/converter for the game's `.PSS` FMV format |

## twinmusic

Opens an `.MH`/`.MB` archive pair as a playlist and lets you audition, reorder, add, remove and re-export tracks, then save the pair back out.

- Plays tracks straight from the archive's own ADPCM data, including sample-accurate loop points read from the archive.
- Waveform scrubber generated from the decoded PCM.
- Multi-select, drag-and-drop reordering, and "null" padding slots preserved so in-game track indices never shift.
- Import external audio into the archive; export any track to WAV.
- Background loading/saving with progress reporting, so large archives don't freeze the UI.

## twinpss

Opens a `.PSS` file (MPEG-2 video plus one or more SPU2-ADPCM/PCM dub tracks) and plays it back, or converts between `.PSS` and MP4.

- Frame-accurate transport: play/pause, single-frame advance, restart, seek bar.
- Switches between multiple language dub tracks.
- Imports an MP4 and converts it to `.PSS`, automatically constraining resolution and frame rate to what the game accepts (640x480/512x288 depending on aspect ratio; 30fps NTSC or 25fps PAL).
- Exports a `.PSS` back out to MP4.
- NTSC/PAL target auto-detected from an opened file's frame rate.

## Building

Requires CMake 3.27+ and a C11 compiler (MSVC, GCC or Clang).

Most dependencies (raylib, flecs, PS2ImageMaker) are fetched automatically via CMake's `FetchContent`. `twinpss` additionally needs FFmpeg's `libavformat`/`libavcodec`/`libavutil`/`libswscale`/`libswresample` available through `pkg-config`; the CI workflow installs these via [vcpkg](https://github.com/microsoft/vcpkg).

### Linux

```sh
sudo apt-get install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev pkg-config nasm
vcpkg install ffmpeg:x64-linux
export PKG_CONFIG_PATH="$VCPKG_INSTALLATION_ROOT/installed/x64-linux/lib/pkgconfig"

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows

```powershell
vcpkg install ffmpeg:x64-windows pkgconf:x64-windows

cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release `
  -DPKG_CONFIG_EXECUTABLE="$env:VCPKG_INSTALLATION_ROOT\installed\x64-windows\tools\pkgconf\pkgconf.exe" `
  -DPKG_CONFIG_PATH="$env:VCPKG_INSTALLATION_ROOT\installed\x64-windows\lib\pkgconfig"
cmake --build build --config Release
```

Building produces three executables: `twinstudio`, `twinmusic` and `twinpss`.

## Project layout

```
common/       Shared engine code: PS2 archive/chunk serialization, rendering, UI, math, audio
descs/        Declarative (.tr) struct descriptions, code-generated into serializers by twinres
tools/twinres Code generator that turns descs/*.tr into the auto_struct_*.c/.h serializers
src/          The three executables: twinstudio (main.c), music_manager (twinmusic), pss_editor (twinpss)
3rdparty/     Vendored single-file dependencies (cJSON, clay, rpmalloc, stb_ds, tinyfiledialogs)
resources/    Fonts and shared runtime resources
```

## Status

This project is under active development. The PS2 archive/chunk format support in `common/` and `descs/` is the shared foundation both editors are built on. Builds are not guaranteed stable on every commit.
