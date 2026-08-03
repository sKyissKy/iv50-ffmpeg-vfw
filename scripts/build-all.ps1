[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -Arch x86 -Configuration $Configuration -SkipTests:$SkipTests
& (Join-Path $PSScriptRoot 'build.ps1') -Arch x64 -Configuration $Configuration -SkipTests:$SkipTests
