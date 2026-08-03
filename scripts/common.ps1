Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:RepoRoot = Split-Path -Parent $PSScriptRoot

function Get-RepoRoot {
    return $script:RepoRoot
}

function Get-VisualStudioPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer (vswhere.exe) was not found.'
    }
    $path = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $path) {
        throw 'Visual Studio 2022 C++ x86/x64 tools were not found.'
    }
    return $path.Trim()
}

function Import-VsEnvironment {
    param(
        [Parameter(Mandatory)]
        [ValidateSet('x86', 'x64')]
        [string]$Arch
    )

    $vsPath = Get-VisualStudioPath
    $vsDevCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
    $commandLine = "call `"$vsDevCmd`" -no_logo -arch=$Arch -host_arch=x64 >nul && set"
    $environmentLines = & $env:ComSpec /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd failed for $Arch."
    }
    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            Set-Item -Path "Env:$name" -Value $value
        }
    }
}

function Get-CMakePath {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    $candidate = Join-Path (Get-VisualStudioPath) 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $candidate) {
        return $candidate
    }
    throw 'CMake was not found.'
}

function Get-NinjaDirectory {
    $command = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ($command) {
        return Split-Path -Parent $command.Source
    }
    $candidate = Join-Path (Get-VisualStudioPath) 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
    if (Test-Path -LiteralPath (Join-Path $candidate 'ninja.exe')) {
        return $candidate
    }
    throw 'Ninja was not found.'
}

function Get-MsysBashPath {
    $candidates = @(
        'C:\msys64\usr\bin\bash.exe',
        'C:\tools\msys64\usr\bin\bash.exe'
    )
    if ($env:ChocolateyInstall) {
        $candidates += Join-Path $env:ChocolateyInstall 'lib\msys2\tools\msys64\usr\bin\bash.exe'
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }
    throw 'MSYS2 bash was not found. Run scripts/bootstrap.ps1 -InstallDependencies.'
}

function Convert-ToMsysPath {
    param([Parameter(Mandatory)][string]$Path)

    $resolved = [System.IO.Path]::GetFullPath($Path).Replace('\', '/')
    if ($resolved -match '^([A-Za-z]):/(.*)$') {
        return "/$($Matches[1].ToLowerInvariant())/$($Matches[2])"
    }
    return $resolved
}

function Get-GitCommit {
    param([string]$Path = $script:RepoRoot)

    $commit = & git -C $Path rev-parse HEAD 2>$null
    if ($LASTEXITCODE -ne 0) {
        return 'uncommitted'
    }
    return $commit.Trim()
}
