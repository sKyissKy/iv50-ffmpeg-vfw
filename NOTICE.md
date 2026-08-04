# Notices

IV50 FFmpeg VFW is licensed under LGPL-2.1-or-later.

This software uses code from the FFmpeg project under LGPL-2.1-or-later.
The source is pinned to FFmpeg tag `n8.0`, commit
`140fd653aed8cad774f991ba083e2d01e86420c7`.

The build deliberately disables GPL and nonfree components. See
`third_party/ffmpeg/LICENSE.md` and `third_party/ffmpeg/COPYING.LGPLv2.1`.

The build applies `third_party/ffmpeg-patches/0001-detect-localized-msvc.patch`
so FFmpeg recognizes a localized MSVC banner when `--toolchain=msvc` is
explicitly selected. The patch does not modify codec implementation code.

Intel and Indeo are trademarks of their respective owners. This project is
not affiliated with or endorsed by Intel. It does not contain Intel's
proprietary codec binaries or source code.
