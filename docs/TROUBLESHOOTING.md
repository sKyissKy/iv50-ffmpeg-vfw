# Troubleshooting

## `ICOpen(VIDC, IV50)` fails

- Confirm the application and DLL architectures match.
- Query both registry views and verify `vidc.iv50` uses a full DLL path.
- Run the matching `vfw_probe.exe`.
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
