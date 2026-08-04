# Troubleshooting

## `ICOpen(VIDC, IV50)` fails

- Confirm the application and DLL architectures match.
- Query both registry views and verify `vidc.iv50` uses a full DLL path.
- Run the matching probe directly, for example:
  `vfw_probe.exe iv50_ffmpeg_vfw_x86.dll sample.avi`. This path does not
  require registration or administrator rights.
- Check `dependencies.txt`; no shared FFmpeg DLL should appear.

## Modern players work but the old application does not

Modern players may use internal FFmpeg or DirectShow and bypass VFW. Confirm
the application reads `Drivers32` and does not directly load `ir50_32.dll`.

## Black output or incorrect colors

Capture the input and output BITMAPINFOHEADER values. The initial version only
supports same-size BI_RGB output at 24 or 32 bits per pixel.

## Build cannot find MSYS2

Install MSYS2 at `C:\msys64` or run `scripts/bootstrap.ps1
-InstallDependencies`. Git for Windows' bash is not sufficient because the
FFmpeg build also requires GNU Make.

## Uninstall refuses to continue

Close the process named by the script. A loaded in-process codec DLL cannot be
safely replaced or deleted.

## Media Player still reports IV50 as unsupported

The Windows 11 Media Player application uses a Media Foundation topology. It
does not use the VFW `Drivers32\vidc.iv50` mapping. The repository includes
`mft_probe.exe` to distinguish registration from application selection:

```powershell
.\mft_probe.exe
```

Successful output includes `MFTEnumEx ... count=1` with the project CLSID and
`CoCreateInstance hr=0x00000000`. That proves both registration and COM
activation. It does not prove that a particular Media Foundation application
will choose the MFT for an AVI file. If the probe succeeds but Media Player
still refuses IV50, use the VFW-compatible legacy application path or convert
the AVI to a modern format; additional registry aliases cannot force the
Media Player topology to select an arbitrary third-party transform.
