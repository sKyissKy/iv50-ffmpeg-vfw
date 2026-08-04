# IV50 FFmpeg VFW

## 中文

这是一个实验性的、仅解码的 Video for Windows（VFW/VCM）兼容编解码器，用于播放 Intel Indeo Video 5（`VIDC:IV50`）视频。项目使用 FFmpeg 原生 Indeo 5 解码器，并将画面转换为 24 位 BGR 或 32 位 BGRA Windows DIB。

项目生成两个不依赖外部 FFmpeg DLL 的编解码器：

- `iv50_ffmpeg_vfw_x86.dll`：供 32 位程序使用，可运行于 x86/x64 Windows。
- `iv50_ffmpeg_vfw_x64.dll`：供 64 位程序使用。

这不是 Intel 原版 `ir50_32.dll` 的重编译，也不提供其私有 API 或编码能力。程序必须通过标准 VFW `VIDC:IV50` 查找编解码器。

项目同时生成实验性的 Media Foundation Transform：`iv50_ffmpeg_mft_x86.dll` 和
`iv50_ffmpeg_mft_x64.dll`。它们通过 COM 的 `DllRegisterServer` 注册，并向
Media Foundation 声明 `MFVideoFormat_IV50` 输入和 RGB32 输出。Media Player
是否选择第三方 MFT 仍取决于其媒体源和系统版本，因此 MFT 不能保证替代 VFW
程序的兼容性。

### 当前状态

VFW 普通解码和旧程序使用的 `ICM_DRAW` 直接绘制接口均已实现，并已在 Windows 11 x64 上通过 IV50 AVI 测试。Media Foundation MFT 已提供 x86/x64 注册入口，但仍属于实验性兼容项目，不建议用于处理不可信视频。编码、DirectShow 和 Intel 私有配置接口不在范围内。

### 构建

```powershell
git clone --recurse-submodules https://github.com/sKyissKy/iv50-ffmpeg-vfw.git
cd iv50-ffmpeg-vfw
pwsh -File scripts/bootstrap.ps1
pwsh -File scripts/build-all.ps1 -Configuration Release
```

构建环境和可复现构建说明见 [docs/BUILDING.md](docs/BUILDING.md)。

### 安装

请在管理员 PowerShell 中运行打包的 `install/install.ps1`。安装器会备份并在卸载时恢复原有的 `vidc.iv50` 映射，验证 SHA-256 和 PE 架构，并且不使用 `regsvr32`。

默认安装位置为：

```text
C:\Windows\SysWOW64\iv50_ffmpeg_vfw_x86.dll
C:\Windows\System32\iv50_ffmpeg_vfw_x64.dll
```

详细说明见 [docs/INSTALL.md](docs/INSTALL.md)。

### 架构

VFW `DriverProc` 接收 IV50 压缩帧，调用 `AV_CODEC_ID_INDEO5`，通过 libswscale 完成像素转换，并提供 BGR24/BGRA DIB 输出；对于旧程序的直接绘制调用，组件使用 `ICM_DRAW` 将解码帧绘制到程序提供的 HDC。详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

### 许可证

项目使用 LGPL-2.1-or-later。FFmpeg 是固定提交的 Git submodule，构建时不启用 GPL 或 nonfree 组件。完整源码发布包包含实际 FFmpeg 源码、补丁、构建脚本、配置参数和许可证声明。

---

## English

This is an experimental, decode-only Video for Windows (VFW/VCM) compatibility codec for Intel Indeo Video 5 (`VIDC:IV50`). It uses FFmpeg's native Indeo 5 decoder and converts decoded frames to 24-bit BGR or 32-bit BGRA Windows DIB output.

The project produces two self-contained VFW codec DLLs:

- `iv50_ffmpeg_vfw_x86.dll` for 32-bit applications on x86/x64 Windows.
- `iv50_ffmpeg_vfw_x64.dll` for 64-bit applications.

This is not a rebuild or drop-in private-API replacement for Intel's `ir50_32.dll`, and it does not provide encoding or Intel-specific private APIs. Applications must use the standard VFW codec lookup for `VIDC:IV50`.

The project also produces experimental Media Foundation Transforms,
`iv50_ffmpeg_mft_x86.dll` and `iv50_ffmpeg_mft_x64.dll`. Their
`DllRegisterServer` entry point registers the COM class and the IV50-to-RGB32
decoder type with Media Foundation. Whether Media Player selects a third-party
MFT still depends on its media source and the Windows version, so this does not
guarantee compatibility for every Media Foundation application.

### Status

Standard VFW decompression and the legacy `ICM_DRAW` direct-rendering interface used by some older programs are implemented and have been tested with IV50 AVI on Windows 11 x64. The experimental Media Foundation MFT is available for both x86 and x64 registration. This remains an experimental compatibility project; do not use it to process untrusted video. Encoding, DirectShow, and Intel-specific configuration APIs are out of scope.

### Build

```powershell
git clone --recurse-submodules https://github.com/sKyissKy/iv50-ffmpeg-vfw.git
cd iv50-ffmpeg-vfw
pwsh -File scripts/bootstrap.ps1
pwsh -File scripts/build-all.ps1 -Configuration Release
```

See [docs/BUILDING.md](docs/BUILDING.md) for prerequisites and reproducible build details.

### Install

Run the packaged `install/install.ps1` from an elevated PowerShell session. The installer backs up and later restores existing `vidc.iv50` mappings, verifies SHA-256 hashes and PE architectures, and does not call `regsvr32`.

The default installation paths are:

```text
C:\Windows\SysWOW64\iv50_ffmpeg_vfw_x86.dll
C:\Windows\System32\iv50_ffmpeg_vfw_x64.dll
```

See [docs/INSTALL.md](docs/INSTALL.md) for details.

### Architecture

The VFW `DriverProc` receives compressed IV50 frames, invokes `AV_CODEC_ID_INDEO5`, converts pixels through libswscale, and provides BGR24 or BGRA DIB output. For legacy applications that use direct rendering, the codec implements `ICM_DRAW` and renders decoded frames to the HDC supplied by the application. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

### License

The project is licensed under LGPL-2.1-or-later. FFmpeg is a pinned Git submodule and is built without GPL or nonfree components. Complete-source release archives contain the actual FFmpeg source, patches, build scripts, configuration parameters, and license notices.
