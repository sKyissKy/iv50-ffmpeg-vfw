# IV50 FFmpeg VFW

An experimental, decode-only Video for Windows (VFW/VCM) codec for Intel
Indeo Video 5 (`VIDC:IV50`). It wraps FFmpeg's native Indeo 5 decoder and
converts decoded frames to 24-bit or 32-bit Windows DIB output.

The project produces two self-contained codec DLLs:

- `iv50_ffmpeg_vfw_x86.dll` for 32-bit applications on x86/x64 Windows.
- `iv50_ffmpeg_vfw_x64.dll` for 64-bit applications.

It is not a rebuild or drop-in private-API replacement for Intel's
`ir50_32.dll`. Applications must use the standard VFW codec lookup for
`VIDC:IV50`.

## Status

This is an experimental compatibility project. Do not use it to process
untrusted video. The implementation is intentionally small and decode-only;
encoding, DirectShow, Media Foundation, scaling, cropping, and Intel-specific
configuration APIs are out of scope.

## Build

```powershell
git clone --recurse-submodules <repository-url>
cd iv50-ffmpeg-vfw
pwsh -File scripts/bootstrap.ps1
pwsh -File scripts/build-all.ps1 -Configuration Release
```

See [docs/BUILDING.md](docs/BUILDING.md) for prerequisites and reproducible
build details.

## Install

Run the packaged `install/install.ps1` from an elevated PowerShell session.
The installer records and later restores any existing `vidc.iv50` mappings.
It verifies packaged hashes and PE architectures, does not call `regsvr32`,
and does not copy files into Windows system directories. See
[docs/INSTALL.md](docs/INSTALL.md).

## Architecture

The VFW `DriverProc` implementation accepts compressed IV50 frames, sends
them to `AV_CODEC_ID_INDEO5`, converts FFmpeg's decoded frame through
libswscale, and returns BGR24/BGRA DIB data. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## License

LGPL-2.1-or-later. FFmpeg is included as a pinned Git submodule and is built
without GPL or nonfree components. Complete-source release archives contain
the actual FFmpeg source, build scripts, configuration, and notices.
