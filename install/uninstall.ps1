#Requires -RunAsAdministrator
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Restore-CodecMapping {
    param([Microsoft.Win32.RegistryView]$View, $State)
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, $View)
    try {
        $key = $base.CreateSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32', $true)
        try {
            if (-not $State.exists) {
                $key.DeleteValue('vidc.iv50', $false)
                return
            }
            $kind = [Microsoft.Win32.RegistryValueKind]::$($State.kind)
            $key.SetValue('vidc.iv50', [string]$State.value, $kind)
        } finally { $key.Dispose() }
    } finally { $base.Dispose() }
}

$systemRoot = $env:windir
$codec32 = Join-Path $systemRoot 'SysWOW64\iv50_ffmpeg_vfw_x86.dll'
$codec64 = Join-Path $systemRoot 'System32\iv50_ffmpeg_vfw_x64.dll'
$stateRoot = Join-Path $env:ProgramData 'IV50 FFmpeg VFW'
$backupPath = Join-Path $stateRoot 'registry-backup.json'
if (-not (Test-Path -LiteralPath $backupPath)) {
    throw 'Registry backup was not found; refusing to guess the previous mapping.'
}

$loadedBy = @()
foreach ($process in Get-Process -ErrorAction SilentlyContinue) {
    try {
        foreach ($module in $process.Modules) {
            if ($module.FileName.Equals($codec32, [StringComparison]::OrdinalIgnoreCase) -or
                $module.FileName.Equals($codec64, [StringComparison]::OrdinalIgnoreCase)) {
                $loadedBy += "$($process.ProcessName) ($($process.Id))"
            }
        }
    } catch { }
}
if ($loadedBy.Count -ne 0) {
    throw "Close processes currently using the codec: $($loadedBy -join ', ')"
}

$backup = Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
Restore-CodecMapping ([Microsoft.Win32.RegistryView]::Registry32) $backup.registry32
Restore-CodecMapping ([Microsoft.Win32.RegistryView]::Registry64) $backup.registry64

foreach ($codecPath in @($codec32, $codec64)) {
    if (Test-Path -LiteralPath $codecPath) {
        Remove-Item -LiteralPath $codecPath -Force
    }
}
Remove-Item -LiteralPath $backupPath -Force
if ((Test-Path -LiteralPath $stateRoot) -and
    -not (Get-ChildItem -LiteralPath $stateRoot -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $stateRoot -Force
}
Write-Host 'IV50 FFmpeg VFW was uninstalled and the previous mappings were restored.'
