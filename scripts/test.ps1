[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x86', 'x64')]
    [string]$Arch,
    [string]$SamplePath,
    [switch]$Integration
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$cmake = Get-CMakePath
$preset = "$Arch-release"
Push-Location $repoRoot
try {
    & $cmake --build --preset $preset --target test
    if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed.' }
} finally { Pop-Location }

if (-not $Integration) { return }

$manifest = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\samples.json') -Raw | ConvertFrom-Json
if (-not $SamplePath) {
    $sampleDirectory = Join-Path $repoRoot 'build\samples'
    New-Item -ItemType Directory -Path $sampleDirectory -Force | Out-Null
    $SamplePath = Join-Path $sampleDirectory $manifest.fileName
    if (-not (Test-Path -LiteralPath $SamplePath)) {
        try {
            Invoke-WebRequest -Uri $manifest.url -OutFile $SamplePath
        } catch {
            $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
            if (-not $curl) { throw }
            & $curl.Source --fail --location --retry 3 --output $SamplePath $manifest.url
            if ($LASTEXITCODE -ne 0) { throw 'Unable to download the IV50 sample.' }
        }
    }
}
$actualHash = (Get-FileHash -LiteralPath $SamplePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $manifest.sha256.ToLowerInvariant()) {
    throw "Sample SHA-256 mismatch: $actualHash"
}

$artifactDirectory = Join-Path $repoRoot "artifacts\$Arch"
$dllPath = Join-Path $artifactDirectory "iv50_ffmpeg_vfw_$Arch.dll"
$probePath = Join-Path $artifactDirectory 'vfw_probe.exe'
$output = & $probePath $dllPath $SamplePath
if ($LASTEXITCODE -ne 0) { throw 'VFW integration probe failed.' }
Write-Host $output
if ($manifest.expectedProbeCrc32 -and $output -notmatch "crc32=$($manifest.expectedProbeCrc32)") {
    throw "Probe CRC did not match $($manifest.expectedProbeCrc32)."
}
