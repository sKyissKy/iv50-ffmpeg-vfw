[CmdletBinding()]
param(
    [string]$X86Dll = (Join-Path $PSScriptRoot 'payload\x86\iv50_ffmpeg_vfw_x86.dll'),
    [string]$X64Dll = (Join-Path $PSScriptRoot 'payload\x64\iv50_ffmpeg_vfw_x64.dll'),
    [string]$MftX86Dll = (Join-Path $PSScriptRoot 'payload\x86\iv50_ffmpeg_mft_x86.dll'),
    [string]$MftX64Dll = (Join-Path $PSScriptRoot 'payload\x64\iv50_ffmpeg_mft_x64.dll'),
    [string]$HashManifest = (Join-Path $PSScriptRoot 'SHA256SUMS.txt'),
    [string]$ExpectedX86Sha256,
    [string]$ExpectedX64Sha256,
    [string]$ExpectedMftX86Sha256,
    [string]$ExpectedMftX64Sha256
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $hostExe = if (Get-Command pwsh.exe -ErrorAction SilentlyContinue) { 'pwsh.exe' } else { 'powershell.exe' }
    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
        '-X86Dll', "`"$X86Dll`"", '-X64Dll', "`"$X64Dll`"",
        '-MftX86Dll', "`"$MftX86Dll`"", '-MftX64Dll', "`"$MftX64Dll`"",
        '-HashManifest', "`"$HashManifest`""
    )
    if ($ExpectedX86Sha256) { $arguments += @('-ExpectedX86Sha256', $ExpectedX86Sha256) }
    if ($ExpectedX64Sha256) { $arguments += @('-ExpectedX64Sha256', $ExpectedX64Sha256) }
    if ($ExpectedMftX86Sha256) { $arguments += @('-ExpectedMftX86Sha256', $ExpectedMftX86Sha256) }
    if ($ExpectedMftX64Sha256) { $arguments += @('-ExpectedMftX64Sha256', $ExpectedMftX64Sha256) }
    $elevated = Start-Process $hostExe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
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
        $reader = [IO.BinaryReader]::new($stream)
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
New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
if (-not (Test-Path -LiteralPath $backupPath)) {
    $backup = [ordered]@{
        createdUtc = [DateTime]::UtcNow.ToString('o')
        registry32 = Get-RegistryValueState ([Microsoft.Win32.RegistryView]::Registry32)
        registry64 = Get-RegistryValueState ([Microsoft.Win32.RegistryView]::Registry64)
    }
    $backup | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $backupPath -Encoding utf8NoBOM
}

$destination32 = Join-Path $systemRoot 'SysWOW64\iv50_ffmpeg_vfw_x86.dll'
$destination64 = Join-Path $systemRoot 'System32\iv50_ffmpeg_vfw_x64.dll'
$mftDestination32 = Join-Path $systemRoot 'SysWOW64\iv50_ffmpeg_mft_x86.dll'
$mftDestination64 = Join-Path $systemRoot 'System32\iv50_ffmpeg_mft_x64.dll'
Copy-Item -LiteralPath $X86Dll -Destination $destination32 -Force
Copy-Item -LiteralPath $X64Dll -Destination $destination64 -Force
Copy-Item -LiteralPath $MftX86Dll -Destination $mftDestination32 -Force
Copy-Item -LiteralPath $MftX64Dll -Destination $mftDestination64 -Force

$regsvr32_32 = Join-Path $systemRoot 'SysWOW64\regsvr32.exe'
$regsvr32_64 = Join-Path $systemRoot 'System32\regsvr32.exe'
$registration32 = Start-Process -FilePath $regsvr32_32 -ArgumentList @('/s', $mftDestination32) -Wait -PassThru
if ($registration32.ExitCode -ne 0) { throw "x86 MFT registration failed with exit code $($registration32.ExitCode)." }
$registration64 = Start-Process -FilePath $regsvr32_64 -ArgumentList @('/s', $mftDestination64) -Wait -PassThru
if ($registration64.ExitCode -ne 0) { throw "x64 MFT registration failed with exit code $($registration64.ExitCode)." }

Set-CodecMapping ([Microsoft.Win32.RegistryView]::Registry32) $destination32
Set-CodecMapping ([Microsoft.Win32.RegistryView]::Registry64) $destination64

Write-Host "Installed x86: $destination32"
Write-Host "Installed x64: $destination64"
Write-Host "x86 SHA-256: $((Get-FileHash -Algorithm SHA256 $destination32).Hash)"
Write-Host "x64 SHA-256: $((Get-FileHash -Algorithm SHA256 $destination64).Hash)"
Write-Host "Registered x86/x64 Media Foundation MFTs."
