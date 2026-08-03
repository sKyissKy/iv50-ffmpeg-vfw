[CmdletBinding()]
param(
    [switch]$InstallDependencies
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

if ($InstallDependencies) {
    try { $msysBash = Get-MsysBashPath } catch { $msysBash = $null }
    if (-not $msysBash) {
        $choco = Get-Command choco.exe -ErrorAction SilentlyContinue
        $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
        if ($choco) {
            & $choco.Source install msys2 --yes --no-progress
        } elseif ($winget) {
            & $winget.Source install --id MSYS2.MSYS2 --exact --silent `
                --accept-package-agreements --accept-source-agreements
        } else {
            throw 'Neither Chocolatey nor winget is available to install MSYS2.'
        }
        if ($LASTEXITCODE -ne 0) {
            throw 'MSYS2 installation failed.'
        }
        $msysBash = Get-MsysBashPath
    }
    & $msysBash --noprofile --norc -lc `
        'pacman --noconfirm -Sy --needed make diffutils'
    if ($LASTEXITCODE -ne 0) { throw 'Unable to install MSYS2 make/diffutils.' }
}

$checks = [ordered]@{}
try { $checks.VisualStudio = Get-VisualStudioPath } catch { $checks.VisualStudio = "MISSING: $($_.Exception.Message)" }
try { $checks.CMake = Get-CMakePath } catch { $checks.CMake = "MISSING: $($_.Exception.Message)" }
try { $checks.Ninja = Join-Path (Get-NinjaDirectory) 'ninja.exe' } catch { $checks.Ninja = "MISSING: $($_.Exception.Message)" }
try { $checks.MSYS2 = Get-MsysBashPath } catch { $checks.MSYS2 = "MISSING: $($_.Exception.Message)" }
$checks.Git = if (Get-Command git.exe -ErrorAction SilentlyContinue) { (Get-Command git.exe).Source } else { 'MISSING: git.exe' }

$missing = @($checks.Values | Where-Object { $_ -like 'MISSING:*' })
[pscustomobject]$checks | Format-List
if ($missing.Count -ne 0) {
    throw "Build environment is incomplete: $($missing -join '; ')"
}

$ffmpegPath = Join-Path (Get-RepoRoot) 'third_party\ffmpeg\configure'
if (-not (Test-Path -LiteralPath $ffmpegPath)) {
    Write-Warning 'FFmpeg submodule is not initialized. Run: git submodule update --init --recursive'
}
