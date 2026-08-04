# Installation

Use the combined release package. Open an elevated PowerShell terminal and run:

```powershell
pwsh -ExecutionPolicy Bypass -File install\install.ps1
```

The installer verifies both DLLs against the packaged `SHA256SUMS.txt`, then
checks their PE machine types before copying or changing either registry view.
For an unpackaged development build, pass both `-ExpectedX86Sha256` and
`-ExpectedX64Sha256` explicitly.

Files are installed into the Windows codec directories:

```text
C:\Windows\SysWOW64\iv50_ffmpeg_vfw_x86.dll
C:\Windows\System32\iv50_ffmpeg_vfw_x64.dll
```

The installer writes a different full DLL path to the 32-bit and 64-bit views
of:

```text
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32
```

The value name is `vidc.iv50`. The prior state is backed up under
`C:\ProgramData\IV50 FFmpeg VFW` and is not overwritten by reinstalling.

To uninstall, close every application using the codec and run:

```powershell
pwsh -ExecutionPolicy Bypass -File install\uninstall.ps1
```

The uninstaller refuses to guess if its registry backup is missing.
