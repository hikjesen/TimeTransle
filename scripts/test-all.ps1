[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$ctest = Get-Command 'ctest' -ErrorAction SilentlyContinue
if (-not $ctest) {
    $localCTest = Join-Path $repoRoot 'out\tools\python\Scripts\ctest.exe'
    if (Test-Path -LiteralPath $localCTest) {
        $ctest = Get-Command $localCTest
    }
}

if (-not $ctest) {
    throw 'CTest is not available. Run scripts/bootstrap.ps1 for prerequisite diagnostics.'
}

$npm = Get-Command 'npm.cmd' -ErrorAction SilentlyContinue
if (-not $npm) {
    throw 'npm.cmd is not available.'
}

$configurationName = $Configuration.ToLowerInvariant()

Push-Location $repoRoot
try {
    foreach ($architecture in 'x64', 'x86') {
        & $ctest.Source --preset "windows-$architecture-$configurationName"
        if ($LASTEXITCODE -ne 0) {
            throw "CTest failed for $architecture."
        }
    }

    $x64Bin = Join-Path $repoRoot "out\build\windows-x64\bin\$Configuration"
    $x86Bin = Join-Path $repoRoot "out\build\windows-x86\bin\$Configuration"
    $controllerE2E = Join-Path $x64Bin 'TimeTransleControllerE2E64.exe'
    $testApp32 = Join-Path $x86Bin 'TimeTransleTestApp32.exe'
    $ordinaryFixture32 = Join-Path $x86Bin 'OrdinaryProgramFixture32.exe'
    $injector32 = Join-Path $x86Bin 'TimeTransleInjector32.exe'
    $hook32 = Join-Path $x86Bin 'TimeTransleHook32.dll'
    foreach ($requiredFile in $controllerE2E, $testApp32, $ordinaryFixture32, $injector32, $hook32) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "Cross-architecture E2E input is missing: $requiredFile"
        }
    }

    Copy-Item -LiteralPath $injector32 -Destination $x64Bin -Force
    Copy-Item -LiteralPath $hook32 -Destination $x64Bin -Force
    & $controllerE2E $ordinaryFixture32 x86 'TimeTransleHook32.dll' ordinary
    if ($LASTEXITCODE -ne 0) {
        throw 'x64 Controller to ordinary x86 target E2E failed.'
    }
    & $controllerE2E $testApp32 x86 'TimeTransleHook32.dll'
    if ($LASTEXITCODE -ne 0) {
        throw 'x64 Controller to x86 target E2E failed.'
    }

    Push-Location (Join-Path $repoRoot 'ui')
    try {
        & $npm.Source run check
        if ($LASTEXITCODE -ne 0) {
            throw 'Frontend checks failed.'
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    Pop-Location
}
