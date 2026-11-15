[CmdletBinding()]
param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $repoRoot 'out/checkpoint-mvp'
$x64 = Join-Path $repoRoot "out/build/windows-x64/bin/$Configuration"
$x86 = Join-Path $repoRoot "out/build/windows-x86/bin/$Configuration"
New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach ($name in @('TimeTransleController.exe', 'TimeTransleInjector64.exe', 'TimeTransleHook64.dll')) {
    Copy-Item -LiteralPath (Join-Path $x64 $name) -Destination $destination
}
foreach ($name in @('TimeTransleInjector32.exe', 'TimeTransleHook32.dll', 'TimeTransleCheckpointFixture32.exe')) {
    Copy-Item -LiteralPath (Join-Path $x86 $name) -Destination $destination
}
New-Item -ItemType Directory -Force -Path (Join-Path $destination 'resources') | Out-Null
$uiDestination = Join-Path $destination 'resources/ui'
New-Item -ItemType Directory -Force -Path $uiDestination | Out-Null
Get-ChildItem -LiteralPath (Join-Path $repoRoot 'ui/dist') | Copy-Item -Destination $uiDestination -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/checkpoint-quickstart.md') -Destination (Join-Path $destination 'README.md')
$noticeDestination = Join-Path $destination 'notices'
New-Item -ItemType Directory -Force -Path $noticeDestination | Out-Null
Get-ChildItem -LiteralPath (Join-Path $repoRoot 'third_party/checkpoint') | Copy-Item -Destination $noticeDestination -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'NOTICE-THIRD-PARTY.md') -Destination $destination
$existingLicenses = Join-Path $repoRoot 'out/package/TimeTransle-0.1.5-win-x64-portable/licenses'
if (Test-Path -LiteralPath $existingLicenses) {
    $licenseDestination = Join-Path $destination 'licenses'
    New-Item -ItemType Directory -Force -Path $licenseDestination | Out-Null
    Get-ChildItem -LiteralPath $existingLicenses | Copy-Item -Destination $licenseDestination -Recurse -Force
}
$acceptance = Join-Path $repoRoot 'docs/checkpoint-acceptance.md'
if (Test-Path -LiteralPath $acceptance) {
    Copy-Item -LiteralPath $acceptance -Destination $destination
}
Write-Output "Checkpoint MVP: $destination"
