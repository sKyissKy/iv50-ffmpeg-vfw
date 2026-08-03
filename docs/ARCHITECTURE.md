# Architecture

The DLL is a classic installable VFW/VCM driver exporting `DriverProc`.

```text
32/64-bit application
  -> msvfw32 / Video Compression Manager
  -> Drivers32: vidc.iv50
  -> iv50_ffmpeg_vfw_x86/x64.dll
  -> libavcodec AV_CODEC_ID_INDEO5
  -> libswscale
  -> BGR24 or BGRA Windows DIB
```

Each `DRV_OPEN` allocates an independent instance. `ICM_DECOMPRESS_BEGIN`
creates the FFmpeg decoder context, `ICM_DECOMPRESS` copies one compressed AVI
chunk into a padded AVPacket and converts one decoded frame, and
`ICM_DECOMPRESS_END` releases all codec state.

The wrapper validates the FOURCC, dimensions, BITMAPINFOHEADER sizes,
compressed frame size, DIB stride, image size, and supported output formats.
It does not scale or crop. Positive DIB heights produce bottom-up output;
negative heights produce top-down output.

No Intel source code or binary is included. The DLL names intentionally do not
impersonate `ir50_32.dll` or a hypothetical Intel x64 codec.
