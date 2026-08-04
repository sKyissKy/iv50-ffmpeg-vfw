[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$packageRoot = Join-Path $repoRoot "build\package\v$Version"
$releaseRoot = Join-Path $repoRoot 'release'
if (Test-Path -LiteralPath $packageRoot) { Remove-Item -LiteralPath $packageRoot -Recurse -Force }
New-Item -ItemType Directory -Path $packageRoot, $releaseRoot -Force | Out-Null

function New-ZipPackage {
    param([string]$Name, [scriptblock]$Populate)
    $stage = Join-Path $packageRoot $Name
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    & $Populate $stage
    $zip = Join-Path $releaseRoot "$Name.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
    return $zip
}

$commonFiles = @('LICENSE', 'NOTICE.md', 'README.md')
$packages = @()
foreach ($arch in @('x86', 'x64')) {
    $packages += New-ZipPackage "iv50-vfw-v$Version-win-$arch" {
        param($stage)
        Copy-Item -Path (Join-Path $repoRoot "artifacts\$arch\*") -Destination $stage
        foreach ($file in $commonFiles) { Copy-Item -LiteralPath (Join-Path $repoRoot $file) -Destination $stage }
    }
}

$packages += New-ZipPackage "iv50-vfw-v$Version-win-installer" {
    param($stage)
    New-Item -ItemType Directory -Path (Join-Path $stage 'payload\x86'), (Join-Path $stage 'payload\x64') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'artifacts\x86\iv50_ffmpeg_vfw_x86.dll') -Destination (Join-Path $stage 'payload\x86')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'artifacts\x64\iv50_ffmpeg_vfw_x64.dll') -Destination (Join-Path $stage 'payload\x64')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'artifacts\x86\iv50_ffmpeg_mft_x86.dll') -Destination (Join-Path $stage 'payload\x86')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'artifacts\x64\iv50_ffmpeg_mft_x64.dll') -Destination (Join-Path $stage 'payload\x64')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'install\install.ps1'), (Join-Path $repoRoot 'install\uninstall.ps1'), (Join-Path $repoRoot 'install\install.cmd'), (Join-Path $repoRoot 'install\uninstall.cmd') -Destination $stage
    $payloads = @(
        (Join-Path $stage 'payload\x86\iv50_ffmpeg_vfw_x86.dll'),
        (Join-Path $stage 'payload\x64\iv50_ffmpeg_vfw_x64.dll'),
        (Join-Path $stage 'payload\x86\iv50_ffmpeg_mft_x86.dll'),
        (Join-Path $stage 'payload\x64\iv50_ffmpeg_mft_x64.dll')
    )
    $payloadHashes = foreach ($payload in $payloads) {
        $hash = (Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $([IO.Path]::GetFileName($payload))"
    }
    $payloadHashes | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ascii
    Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\INSTALL.md') -Destination $stage
    foreach ($file in $commonFiles) { Copy-Item -LiteralPath (Join-Path $repoRoot $file) -Destination $stage }
}

$sourceStage = Join-Path $packageRoot "iv50-vfw-v$Version-source-complete"
New-Item -ItemType Directory -Path $sourceStage -Force | Out-Null
$archive = Join-Path $packageRoot 'wrapper.zip'
& git -C $repoRoot archive --format=zip --output=$archive HEAD
if ($LASTEXITCODE -ne 0) { throw 'git archive failed; commit the release sources first.' }
Expand-Archive -LiteralPath $archive -DestinationPath $sourceStage
$ffmpegDestination = Join-Path $sourceStage 'third_party\ffmpeg'
New-Item -ItemType Directory -Path $ffmpegDestination -Force | Out-Null
$ffmpegSource = Join-Path $repoRoot 'third_party\ffmpeg'
Get-ChildItem -LiteralPath $ffmpegSource -Force |
    Where-Object Name -ne '.git' |
    Copy-Item -Destination $ffmpegDestination -Recurse -Force
$sourceZip = Join-Path $releaseRoot "iv50-vfw-v$Version-source-complete.zip"
if (Test-Path -LiteralPath $sourceZip) { Remove-Item -LiteralPath $sourceZip -Force }
Compress-Archive -Path (Join-Path $sourceStage '*') -DestinationPath $sourceZip -CompressionLevel Optimal
$packages += $sourceZip

$releaseBuildInfo = [ordered]@{
    version = $Version
    projectCommit = Get-GitCommit
    ffmpegCommit = Get-GitCommit (Join-Path $repoRoot 'third_party\ffmpeg')
    builds = @(
        (Get-Content -LiteralPath (Join-Path $repoRoot 'artifacts\x86\build-info.json') -Raw | ConvertFrom-Json),
        (Get-Content -LiteralPath (Join-Path $repoRoot 'artifacts\x64\build-info.json') -Raw | ConvertFrom-Json)
    )
}
$releaseBuildInfoPath = Join-Path $releaseRoot 'build-info.json'
$releaseBuildInfo | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $releaseBuildInfoPath -Encoding utf8NoBOM
$packages += $releaseBuildInfoPath

$hashLines = foreach ($package in $packages) {
    $hash = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($package))"
}
$hashLines | Set-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt') -Encoding ascii
Write-Host "Release packages: $releaseRoot"
