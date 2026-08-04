# FFmpeg patches

`0001-detect-localized-msvc.patch` makes compiler probing deterministic when a
localized `cl.exe` ignores `VSLANG=1033`. It only activates when the caller
explicitly selected FFmpeg's `--toolchain=msvc`; no decoder code is changed.
