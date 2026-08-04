[CmdletBinding()]
param(
    [string]$X86Dll,
    [string]$X64Dll,
    [string]$MftX86Dll,
    [string]$MftX64Dll,
    [string]$HashManifest,
    [string]$ExpectedX86Sha256,
    [string]$ExpectedX64Sha256,
    [string]$ExpectedMftX86Sha256,
    [string]$ExpectedMftX64Sha256
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Resolve package-relative defaults after parameter binding. Windows
# PowerShell can leave $PSScriptRoot empty while evaluating param defaults.
if ([string]::IsNullOrWhiteSpace($X86Dll)) { $X86Dll = Join-Path $PSScriptRoot 'payload\x86\iv50_ffmpeg_vfw_x86.dll' }
if ([string]::IsNullOrWhiteSpace($X64Dll)) { $X64Dll = Join-Path $PSScriptRoot 'payload\x64\iv50_ffmpeg_vfw_x64.dll' }
if ([string]::IsNullOrWhiteSpace($MftX86Dll)) { $MftX86Dll = Join-Path $PSScriptRoot 'payload\x86\iv50_ffmpeg_mft_x86.dll' }
if ([string]::IsNullOrWhiteSpace($MftX64Dll)) { $MftX64Dll = Join-Path $PSScriptRoot 'payload\x64\iv50_ffmpeg_mft_x64.dll' }
if ([string]::IsNullOrWhiteSpace($HashManifest)) { $HashManifest = Join-Path $PSScriptRoot 'SHA256SUMS.txt' }

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $hostExe = 'powershell.exe'
    if (Get-Command pwsh.exe -ErrorAction SilentlyContinue) { $hostExe = 'pwsh.exe' }
    # Use one argument string for Start-Process. This works consistently in
    # Windows PowerShell 5.1 and PowerShell 7, including paths with spaces.
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}"' -f $PSCommandPath
    $arguments += ' -X86Dll "{0}" -X64Dll "{1}"' -f $X86Dll, $X64Dll
    $arguments += ' -MftX86Dll "{0}" -MftX64Dll "{1}"' -f $MftX86Dll, $MftX64Dll
    $arguments += ' -HashManifest "{0}"' -f $HashManifest
    if ($ExpectedX86Sha256) { $arguments += ' -ExpectedX86Sha256 "{0}"' -f $ExpectedX86Sha256 }
    if ($ExpectedX64Sha256) { $arguments += ' -ExpectedX64Sha256 "{0}"' -f $ExpectedX64Sha256 }
    if ($ExpectedMftX86Sha256) { $arguments += ' -ExpectedMftX86Sha256 "{0}"' -f $ExpectedMftX86Sha256 }
    if ($ExpectedMftX64Sha256) { $arguments += ' -ExpectedMftX64Sha256 "{0}"' -f $ExpectedMftX64Sha256 }
    $elevated = Start-Process -FilePath $hostExe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $elevated.ExitCode
}

function Get-ManifestHashes {
    param([Parameter(Mandatory)][string]$Path)

    $hashes = @{}
    if (-not (Test-Path -LiteralPath $Path)) {
        return $hashes
    }
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^([0-9A-Fa-f]{64})\s+\*?(.+)$') {
            $hashes[[IO.Path]::GetFileName($Matches[2].Trim())] = $Matches[1].ToLowerInvariant()
        }
    }
    return $hashes
}

function Assert-FileHash {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Expected
    )

    if ($Expected -notmatch '^[0-9A-Fa-f]{64}$') {
        throw "Invalid expected SHA-256 for $Path."
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected.ToLowerInvariant()) {
        throw "SHA-256 mismatch for $Path. Expected $Expected, found $actual."
    }
}

function Get-PeMachine {
    param([Parameter(Mandatory)][string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    try {
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "$Path is not a PE file." }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "$Path has an invalid PE signature." }
        return $reader.ReadUInt16()
    } finally {
        $stream.Dispose()
    }
}

function Get-RegistryValueState {
    param([Microsoft.Win32.RegistryView]$View)
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, $View)
    try {
        $key = $base.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32', $false)
        try {
            $exists = $key -and ($key.GetValueNames() -contains 'vidc.iv50')
            if (-not $exists) { return @{ exists = $false } }
            return @{
                exists = $true
                kind = $key.GetValueKind('vidc.iv50').ToString()
                value = [string]$key.GetValue('vidc.iv50', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            }
        } finally { if ($key) { $key.Dispose() } }
    } finally { $base.Dispose() }
}

function Set-CodecMapping {
    param([Microsoft.Win32.RegistryView]$View, [string]$DllPath)
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, $View)
    try {
        $key = $base.CreateSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32', $true)
        try {
            $key.SetValue('vidc.iv50', $DllPath, [Microsoft.Win32.RegistryValueKind]::String)
        } finally { $key.Dispose() }
    } finally { $base.Dispose() }
}

function Get-CodecMapping {
    param([Microsoft.Win32.RegistryView]$View)
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, $View)
    try {
        $key = $base.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32', $false)
        try {
            if ($key) { return [string]$key.GetValue('vidc.iv50') }
            return ''
        }
        finally { if ($key) { $key.Dispose() } }
    } finally { $base.Dispose() }
}

function Test-InstalledDll {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$ExpectedHash,
        [Parameter(Mandatory)][UInt16]$ExpectedMachine
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try {
        return (Get-PeMachine $Path) -eq $ExpectedMachine -and
            (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.Equals(
                $ExpectedHash, [StringComparison]::OrdinalIgnoreCase)
    } catch { return $false }
}

function Get-MftInprocPath {
    param([Microsoft.Win32.RegistryView]$View, [string]$Clsid)
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, $View)
    try {
        $key = $base.OpenSubKey("Software\Classes\CLSID\$Clsid\InprocServer32", $false)
        try {
            if ($key) { return [string]$key.GetValue('') }
            return ''
        }
        finally { if ($key) { $key.Dispose() } }
    } finally { $base.Dispose() }
}

$X86Dll = (Resolve-Path -LiteralPath $X86Dll).Path
$X64Dll = (Resolve-Path -LiteralPath $X64Dll).Path
$MftX86Dll = (Resolve-Path -LiteralPath $MftX86Dll).Path
$MftX64Dll = (Resolve-Path -LiteralPath $MftX64Dll).Path
$manifestHashes = Get-ManifestHashes $HashManifest
if (-not $ExpectedX86Sha256) { $ExpectedX86Sha256 = $manifestHashes[[IO.Path]::GetFileName($X86Dll)] }
if (-not $ExpectedX64Sha256) { $ExpectedX64Sha256 = $manifestHashes[[IO.Path]::GetFileName($X64Dll)] }
if (-not $ExpectedMftX86Sha256) { $ExpectedMftX86Sha256 = $manifestHashes[[IO.Path]::GetFileName($MftX86Dll)] }
if (-not $ExpectedMftX64Sha256) { $ExpectedMftX64Sha256 = $manifestHashes[[IO.Path]::GetFileName($MftX64Dll)] }
if (-not $ExpectedX86Sha256 -or -not $ExpectedX64Sha256 -or
    -not $ExpectedMftX86Sha256 -or -not $ExpectedMftX64Sha256) {
    throw 'Expected SHA-256 values for all four DLLs are required. Use the packaged SHA256SUMS.txt or pass them explicitly.'
}
Assert-FileHash $X86Dll $ExpectedX86Sha256
Assert-FileHash $X64Dll $ExpectedX64Sha256
Assert-FileHash $MftX86Dll $ExpectedMftX86Sha256
Assert-FileHash $MftX64Dll $ExpectedMftX64Sha256
if ((Get-PeMachine $X86Dll) -ne 0x014C) { throw 'The x86 DLL has the wrong PE machine type.' }
if ((Get-PeMachine $X64Dll) -ne 0x8664) { throw 'The x64 DLL has the wrong PE machine type.' }
if ((Get-PeMachine $MftX86Dll) -ne 0x014C) { throw 'The x86 MFT DLL has the wrong PE machine type.' }
if ((Get-PeMachine $MftX64Dll) -ne 0x8664) { throw 'The x64 MFT DLL has the wrong PE machine type.' }

$systemRoot = $env:windir
$stateRoot = Join-Path $env:ProgramData 'IV50 FFmpeg VFW'
$backupPath = Join-Path $stateRoot 'registry-backup.json'

$destination32 = Join-Path $systemRoot 'SysWOW64\iv50_ffmpeg_vfw_x86.dll'
$destination64 = Join-Path $systemRoot 'System32\iv50_ffmpeg_vfw_x64.dll'
$mftDestination32 = Join-Path $systemRoot 'SysWOW64\iv50_ffmpeg_mft_x86.dll'
$mftDestination64 = Join-Path $systemRoot 'System32\iv50_ffmpeg_mft_x64.dll'
$mftClsid = '{7A7B8E50-4D50-4F5A-9B4C-551A50395001}'
$vfw32NeedsCopy = -not (Test-InstalledDll $destination32 $ExpectedX86Sha256 0x014C)
$vfw64NeedsCopy = -not (Test-InstalledDll $destination64 $ExpectedX64Sha256 0x8664)
$mft32NeedsCopy = -not (Test-InstalledDll $mftDestination32 $ExpectedMftX86Sha256 0x014C)
$mft64NeedsCopy = -not (Test-InstalledDll $mftDestination64 $ExpectedMftX64Sha256 0x8664)
if ($vfw32NeedsCopy) { Copy-Item -LiteralPath $X86Dll -Destination $destination32 -Force } else { Write-Host "Skipped existing x86 VFW DLL: $destination32" }
if ($vfw64NeedsCopy) { Copy-Item -LiteralPath $X64Dll -Destination $destination64 -Force } else { Write-Host "Skipped existing x64 VFW DLL: $destination64" }
if ($mft32NeedsCopy) { Copy-Item -LiteralPath $MftX86Dll -Destination $mftDestination32 -Force } else { Write-Host "Skipped existing x86 MFT DLL: $mftDestination32" }
if ($mft64NeedsCopy) { Copy-Item -LiteralPath $MftX64Dll -Destination $mftDestination64 -Force } else { Write-Host "Skipped existing x64 MFT DLL: $mftDestination64" }

$backupRequired = ($vfw32NeedsCopy -or $vfw64NeedsCopy -or
    (Get-CodecMapping ([Microsoft.Win32.RegistryView]::Registry32) -ne $destination32) -or
    (Get-CodecMapping ([Microsoft.Win32.RegistryView]::Registry64) -ne $destination64))
if ($backupRequired -and -not (Test-Path -LiteralPath $backupPath)) {
    New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
    $backup = [ordered]@{
        createdUtc = [DateTime]::UtcNow.ToString('o')
        registry32 = Get-RegistryValueState ([Microsoft.Win32.RegistryView]::Registry32)
        registry64 = Get-RegistryValueState ([Microsoft.Win32.RegistryView]::Registry64)
    }
    # Windows PowerShell 5.1 does not support the utf8NoBOM encoding name.
    # Use UTF8 there (the BOM is valid JSON), while keeping a BOM-free file
    # under PowerShell 7+.
    $json = $backup | ConvertTo-Json -Depth 5
    if ($PSVersionTable.PSVersion.Major -ge 6) {
        [IO.File]::WriteAllText($backupPath, $json, (New-Object Text.UTF8Encoding($false)))
    } else {
        Set-Content -LiteralPath $backupPath -Value $json -Encoding UTF8
    }
}

$regsvr32_32 = Join-Path $systemRoot 'SysWOW64\regsvr32.exe'
$regsvr32_64 = Join-Path $systemRoot 'System32\regsvr32.exe'
if ($mft32NeedsCopy -or ((Get-MftInprocPath ([Microsoft.Win32.RegistryView]::Registry32) $mftClsid) -ne $mftDestination32)) {
    $registration32 = Start-Process -FilePath $regsvr32_32 -ArgumentList @('/s', $mftDestination32) -Wait -PassThru
    if ($registration32.ExitCode -ne 0) { throw "x86 MFT registration failed with exit code $($registration32.ExitCode)." }
} else { Write-Host 'Skipped existing x86 MFT registration.' }
if ($mft64NeedsCopy -or ((Get-MftInprocPath ([Microsoft.Win32.RegistryView]::Registry64) $mftClsid) -ne $mftDestination64)) {
    $registration64 = Start-Process -FilePath $regsvr32_64 -ArgumentList @('/s', $mftDestination64) -Wait -PassThru
    if ($registration64.ExitCode -ne 0) { throw "x64 MFT registration failed with exit code $($registration64.ExitCode)." }
} else { Write-Host 'Skipped existing x64 MFT registration.' }

if ((Get-CodecMapping ([Microsoft.Win32.RegistryView]::Registry32)) -ne $destination32) { Set-CodecMapping ([Microsoft.Win32.RegistryView]::Registry32) $destination32 } else { Write-Host 'Skipped existing x86 VFW mapping.' }
if ((Get-CodecMapping ([Microsoft.Win32.RegistryView]::Registry64)) -ne $destination64) { Set-CodecMapping ([Microsoft.Win32.RegistryView]::Registry64) $destination64 } else { Write-Host 'Skipped existing x64 VFW mapping.' }

Write-Host "Installed x86: $destination32"
Write-Host "Installed x64: $destination64"
Write-Host "x86 SHA-256: $((Get-FileHash -Algorithm SHA256 $destination32).Hash)"
Write-Host "x64 SHA-256: $((Get-FileHash -Algorithm SHA256 $destination64).Hash)"
Write-Host "Registered x86/x64 Media Foundation MFTs."
