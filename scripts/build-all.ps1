[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$cmake = Get-Command 'cmake' -ErrorAction SilentlyContinue
if (-not $cmake) {
    $localCMake = Join-Path $repoRoot 'out\tools\python\Scripts\cmake.exe'
    if (Test-Path -LiteralPath $localCMake) {
        $cmake = Get-Command $localCMake
    }
}

if (-not $cmake) {
    throw 'CMake is not available. Run scripts/bootstrap.ps1 for prerequisite diagnostics.'
}

$configurationName = $Configuration.ToLowerInvariant()

Push-Location $repoRoot
try {
    foreach ($architecture in 'x64', 'x86') {
        $configurePreset = "windows-$architecture"
        $buildPreset = "$configurePreset-$configurationName"

        & $cmake.Source --preset $configurePreset
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed for $architecture."
        }

        & $cmake.Source --build --preset $buildPreset
        if ($LASTEXITCODE -ne 0) {
            throw "CMake build failed for $architecture."
        }
    }
}
finally {
    Pop-Location
}
