[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x86', 'x64')]
    [string]$Arch,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$ffmpegSource = Join-Path $repoRoot 'third_party\ffmpeg'
$configurePath = Join-Path $ffmpegSource 'configure'
if (-not (Test-Path -LiteralPath $configurePath)) {
    & git -C $repoRoot submodule update --init --recursive
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $configurePath)) {
        throw 'Unable to initialize the FFmpeg submodule.'
    }
}

$buildRoot = Join-Path $repoRoot "build\ffmpeg\$Arch"
$installRoot = Join-Path $buildRoot 'install'
$stampPath = Join-Path $installRoot '.iv50-ffmpeg-build.json'
$ffmpegCommit = (& git -C $ffmpegSource rev-parse HEAD).Trim()
$expectedFfmpegCommit = '140fd653aed8cad774f991ba083e2d01e86420c7'
if ($ffmpegCommit -ne $expectedFfmpegCommit) {
    throw "FFmpeg must be pinned to $expectedFfmpegCommit, found $ffmpegCommit."
}
$patchDirectory = Join-Path $repoRoot 'third_party\ffmpeg-patches'
foreach ($patch in Get-ChildItem -LiteralPath $patchDirectory -Filter '*.patch' | Sort-Object Name) {
    & git -C $ffmpegSource apply --check $patch.FullName 2>$null
    if ($LASTEXITCODE -eq 0) {
        & git -C $ffmpegSource apply $patch.FullName
        if ($LASTEXITCODE -ne 0) { throw "Unable to apply $($patch.Name)." }
        continue
    }
    & git -C $ffmpegSource apply --reverse --check $patch.FullName 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg patch is neither applicable nor already applied: $($patch.Name)"
    }
}
$configuration = [ordered]@{
    ffmpegCommit = $ffmpegCommit
    arch = $Arch
    configure = @(
        '--toolchain=msvc',
        "--arch=$(if ($Arch -eq 'x86') { 'x86_32' } else { 'x86_64' })",
        '--target-os=win32',
        '--enable-static', '--disable-shared', '--disable-autodetect', '--disable-everything',
        '--enable-avcodec', '--enable-avutil', '--enable-swscale', '--enable-decoder=indeo5',
        '--disable-avdevice', '--disable-avfilter', '--disable-avformat',
        '--disable-swresample',
        '--disable-programs', '--disable-doc', '--disable-network', '--disable-debug',
        '--disable-gpl', '--disable-nonfree', '--disable-asm', '--disable-x86asm',
        '--extra-cflags=-MT -utf-8 -wd4101 -wd4090 -wd4133 -wd4333 -wd4334'
    )
}
$configurationJson = $configuration | ConvertTo-Json -Depth 5 -Compress
if (-not $Force -and (Test-Path -LiteralPath $stampPath)) {
    if ((Get-Content -LiteralPath $stampPath -Raw).Trim() -eq $configurationJson) {
        Write-Host "FFmpeg $Arch is already current."
        $global:LASTEXITCODE = 0
        return
    }
}

Import-VsEnvironment -Arch $Arch
$env:VSLANG = '1033'
$bash = Get-MsysBashPath
$env:IV50_FFMPEG_SOURCE = Convert-ToMsysPath $ffmpegSource
$env:IV50_FFMPEG_BUILD = Convert-ToMsysPath (Join-Path $buildRoot 'obj')
$env:IV50_FFMPEG_PREFIX = Convert-ToMsysPath $installRoot
$env:IV50_FFMPEG_ARCH = if ($Arch -eq 'x86') { 'x86_32' } else { 'x86_64' }
$jobs = [Math]::Max(1, [Environment]::ProcessorCount)

$shellScript = @'
set -euo pipefail
export PATH="/usr/bin:$PATH"
command -v make >/dev/null
rm -rf "$IV50_FFMPEG_BUILD" "$IV50_FFMPEG_PREFIX"
mkdir -p "$IV50_FFMPEG_BUILD" "$IV50_FFMPEG_PREFIX"
cd "$IV50_FFMPEG_BUILD"
"$IV50_FFMPEG_SOURCE/configure" \
  --toolchain=msvc \
  --arch="$IV50_FFMPEG_ARCH" \
  --target-os=win32 \
  --prefix="$IV50_FFMPEG_PREFIX" \
  --enable-static --disable-shared \
  --disable-autodetect --disable-everything \
  --enable-avcodec --enable-avutil --enable-swscale --enable-decoder=indeo5 \
  --disable-avdevice --disable-avfilter --disable-avformat \
  --disable-swresample \
  --disable-programs --disable-doc --disable-network --disable-debug \
  --disable-gpl --disable-nonfree --disable-asm --disable-x86asm \
  --extra-cflags="-MT -utf-8 -wd4101 -wd4090 -wd4133 -wd4333 -wd4334"
make -j"$IV50_BUILD_JOBS"
make install-libavcodec install-libavutil install-libswscale install-headers
'@
$env:IV50_BUILD_JOBS = $jobs
& $bash --noprofile --norc -lc $shellScript
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg $Arch build failed."
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
Set-Content -LiteralPath $stampPath -Value $configurationJson -Encoding utf8NoBOM
