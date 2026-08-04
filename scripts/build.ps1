[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x86', 'x64')]
    [string]$Arch,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipFFmpeg,
    [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
if (-not $SkipFFmpeg) {
    & (Join-Path $PSScriptRoot 'build-ffmpeg.ps1') -Arch $Arch
}

Import-VsEnvironment -Arch $Arch
$cmake = Get-CMakePath
$env:Path = "$(Get-NinjaDirectory);$env:Path"
$preset = "$Arch-$($Configuration.ToLowerInvariant())"

Push-Location $repoRoot
try {
    & $cmake --preset $preset --fresh
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $cmake --build --preset $preset --parallel
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
    if (-not $SkipTests) {
        & $cmake --build --preset $preset --target test
        if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed.' }
    }
} finally {
    Pop-Location
}

$buildDirectory = Join-Path $repoRoot "build\cmake\$preset"
$artifactDirectory = Join-Path $repoRoot "artifacts\$Arch"
New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null
$dll = Get-ChildItem -LiteralPath $buildDirectory -Filter "iv50_ffmpeg_vfw_$Arch.dll" -Recurse | Select-Object -First 1
$mft = Get-ChildItem -LiteralPath $buildDirectory -Filter "iv50_ffmpeg_mft_$Arch.dll" -Recurse | Select-Object -First 1
$probe = Get-ChildItem -LiteralPath $buildDirectory -Filter 'vfw_probe.exe' -Recurse | Select-Object -First 1
$mftProbe = Get-ChildItem -LiteralPath $buildDirectory -Filter 'mft_probe.exe' -Recurse | Select-Object -First 1
if (-not $dll -or -not $mft -or -not $probe -or -not $mftProbe) {
    throw 'Expected VFW DLL, MFT DLL, or VFW probe was not produced.'
}
Copy-Item -LiteralPath $dll.FullName -Destination $artifactDirectory -Force
Copy-Item -LiteralPath $mft.FullName -Destination $artifactDirectory -Force
Copy-Item -LiteralPath $probe.FullName -Destination $artifactDirectory -Force
Copy-Item -LiteralPath $mftProbe.FullName -Destination $artifactDirectory -Force
$pdb = Get-ChildItem -LiteralPath $buildDirectory -Filter "iv50_ffmpeg_vfw_$Arch.pdb" -Recurse | Select-Object -First 1
if ($pdb) {
    Copy-Item -LiteralPath $pdb.FullName -Destination $artifactDirectory -Force
}
$mftPdb = Get-ChildItem -LiteralPath $buildDirectory -Filter "iv50_ffmpeg_mft_$Arch.pdb" -Recurse | Select-Object -First 1
if ($mftPdb) {
    Copy-Item -LiteralPath $mftPdb.FullName -Destination $artifactDirectory -Force
}

$dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
$compilerPath = (Get-Command cl.exe -ErrorAction Stop).Source
$dependencies = (& $dumpbin /dependents $dll.FullName) -join "`n"
if ($dependencies -match '(?i)avcodec|avutil|swscale') {
    throw 'The codec unexpectedly depends on shared FFmpeg DLLs.'
}
Set-Content -LiteralPath (Join-Path $artifactDirectory 'dependencies.txt') `
    -Value $dependencies -Encoding utf8NoBOM

$buildInfo = [ordered]@{
    projectCommit = Get-GitCommit
    ffmpegCommit = Get-GitCommit (Join-Path $repoRoot 'third_party\ffmpeg')
    architecture = $Arch
    configuration = $Configuration
    cmake = [string](& $cmake --version | Select-Object -First 1)
    compiler = (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion
    windowsSdk = $env:WindowsSDKVersion
    ffmpegConfiguration = Get-Content -LiteralPath `
        (Join-Path $repoRoot "build\ffmpeg\$Arch\install\.iv50-ffmpeg-build.json") -Raw |
        ConvertFrom-Json
    createdUtc = [DateTime]::UtcNow.ToString('o')
}
$buildInfo | ConvertTo-Json -Depth 10 | Set-Content `
    -LiteralPath (Join-Path $artifactDirectory 'build-info.json') -Encoding utf8NoBOM

Write-Host "Artifacts: $artifactDirectory"
