[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$minimumCMake = [version]'3.28.0'
$minimumNinja = [version]'1.11.0'
$missing = [System.Collections.Generic.List[string]]::new()
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$localToolScripts = Join-Path $repositoryRoot 'out\tools\python\Scripts'

function Get-ToolVersion {
    param(
        [Parameter(Mandatory)]
        [string] $CommandName,

        [string[]] $CandidatePaths = @()
    )

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    $toolPath = if ($command) { $command.Source } else { $null }

    if (-not $toolPath) {
        $toolPath = $CandidatePaths |
            Where-Object { Test-Path -LiteralPath $_ } |
            Select-Object -First 1
    }

    if (-not $toolPath) {
        return $null
    }

    $versionText = (& $toolPath --version 2>$null | Select-Object -First 1)
    return [pscustomobject]@{
        Name = $CommandName
        Path = $toolPath
        VersionText = $versionText
    }
}

$node = Get-ToolVersion -CommandName 'node'
$npm = Get-ToolVersion -CommandName 'npm.cmd'

if (-not $node) {
    $missing.Add('Node.js 24')
}

if (-not $npm) {
    $missing.Add('npm 11')
}

$programFilesX86 = ${env:ProgramFiles(x86)}
$vswhereCandidates = @(
    (Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
)
$vswhere = $vswhereCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

$visualStudioPath = $null
if ($vswhere) {
    $visualStudioPath = (& $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
}

if (-not $visualStudioPath) {
    $missing.Add('Visual Studio 2022 Build Tools with MSVC x86/x64')
}

$cmakeCandidates = @(
    (Join-Path $localToolScripts 'cmake.exe')
)
$ninjaCandidates = @(
    (Join-Path $localToolScripts 'ninja.exe')
)
if ($visualStudioPath) {
    $cmakeCandidates += Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $ninjaCandidates += Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
}

$cmake = Get-ToolVersion -CommandName 'cmake' -CandidatePaths $cmakeCandidates
$ninja = Get-ToolVersion -CommandName 'ninja' -CandidatePaths $ninjaCandidates

if (-not $cmake) {
    $missing.Add('CMake >= 3.28')
}
elseif ($cmake.VersionText -match '(\d+\.\d+\.\d+)' -and [version]$Matches[1] -lt $minimumCMake) {
    $missing.Add("CMake >= 3.28 (found $($Matches[1]))")
}

if (-not $ninja) {
    $missing.Add('Ninja >= 1.11')
}
elseif ($ninja.VersionText -match '(\d+\.\d+(?:\.\d+)?)' -and [version]$Matches[1] -lt $minimumNinja) {
    $missing.Add("Ninja >= 1.11 (found $($Matches[1]))")
}

$sdkRoots = Get-ItemProperty `
    -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' `
    -ErrorAction SilentlyContinue
$windowsSdkPath = $null
if ($sdkRoots) {
    $kitsRoot11Property = $sdkRoots.PSObject.Properties['KitsRoot11']
    $kitsRoot10Property = $sdkRoots.PSObject.Properties['KitsRoot10']
    if ($kitsRoot11Property) {
        $windowsSdkPath = $kitsRoot11Property.Value
    }
    elseif ($kitsRoot10Property) {
        $windowsSdkPath = $kitsRoot10Property.Value
    }
}

if (-not $windowsSdkPath) {
    $missing.Add('Windows 10/11 SDK')
}

$result = [pscustomobject]@{
    Ready = ($missing.Count -eq 0)
    CMake = $cmake
    Ninja = $ninja
    Node = $node
    Npm = $npm
    VisualStudio = $visualStudioPath
    WindowsSdk = $windowsSdkPath
    Missing = $missing
}

$result | Format-List

if ($missing.Count -gt 0) {
    Write-Error (
        "TimeTransle native prerequisites are incomplete: " +
        ($missing -join ', ') +
        '. Install the listed tools before running the C++ configure/build presets.'
    )
}
