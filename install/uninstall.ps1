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

$installRoot = Join-Path $env:ProgramFiles 'IV50 FFmpeg VFW'
$stateRoot = Join-Path $env:ProgramData 'IV50 FFmpeg VFW'
$backupPath = Join-Path $stateRoot 'registry-backup.json'
if (-not (Test-Path -LiteralPath $backupPath)) {
    throw 'Registry backup was not found; refusing to guess the previous mapping.'
}

$loadedBy = @()
foreach ($process in Get-Process -ErrorAction SilentlyContinue) {
    try {
        foreach ($module in $process.Modules) {
            if ($module.FileName.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) {
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

if (Test-Path -LiteralPath $installRoot) {
    Remove-Item -LiteralPath $installRoot -Recurse -Force
}
Remove-Item -LiteralPath $backupPath -Force
if ((Test-Path -LiteralPath $stateRoot) -and
    -not (Get-ChildItem -LiteralPath $stateRoot -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $stateRoot -Force
}
Write-Host 'IV50 FFmpeg VFW was uninstalled and the previous mappings were restored.'
