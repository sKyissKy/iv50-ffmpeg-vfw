# Building

## Required environment

- Windows 11 x64.
- Visual Studio 2022 Build Tools with `.vsconfig` components.
- Windows 11 SDK 26100 or newer compatible SDK.
- CMake 3.25 or newer and Ninja (the Visual Studio bundled copies work).
- Git.
- MSYS2 at `C:\msys64` or `C:\tools\msys64`, used only for FFmpeg
  `configure` and GNU Make.
- PowerShell 7.

Check the environment without changing it:

```powershell
pwsh -File scripts/bootstrap.ps1
```

Install MSYS2 explicitly when needed:

```powershell
pwsh -File scripts/bootstrap.ps1 -InstallDependencies
```

The install switch is the only bootstrap mode that changes the system.

## Source initialization

```powershell
git submodule update --init --recursive
git -C third_party/ffmpeg rev-parse HEAD
```

The expected FFmpeg commit is:

```text
140fd653aed8cad774f991ba083e2d01e86420c7
```

## Build commands

```powershell
pwsh -File scripts/build.ps1 -Arch x86 -Configuration Release
pwsh -File scripts/build.ps1 -Arch x64 -Configuration Release
pwsh -File scripts/build-all.ps1 -Configuration Release
```

`build-ffmpeg.ps1` configures a static, minimal FFmpeg build containing only
libavcodec, libavutil, libswscale, and the Indeo 5 decoder. It disables
autodetection, GPL, nonfree, programs, network support, other decoders,
encoders, demuxers, filters, and assembly.

Outputs are staged in `artifacts/x86` and `artifacts/x64`. Each directory
contains the codec DLL, VFW probe, dependency report, and `build-info.json`.

Run the official FFmpeg IV50 sample integration test without installing the
codec or changing the registry:

```powershell
pwsh -File scripts/test.ps1 -Arch x86 -Integration
pwsh -File scripts/test.ps1 -Arch x64 -Integration
```

The probe loads the selected DLL and opens its `DriverProc` through
`ICOpenFunction`. It validates all 134 frames against the fixed CRC in
`tests/samples.json`.

## Visual Studio

`IV50Vfw.sln` is a makefile-style convenience solution. Its Win32 and x64
configurations call the same PowerShell/CMake scripts, so command-line and IDE
builds do not diverge.
