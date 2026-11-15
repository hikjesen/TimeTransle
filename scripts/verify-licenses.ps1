[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $repoRoot 'third_party\dependencies.lock.json'
$noticePath = Join-Path $repoRoot 'NOTICE-THIRD-PARTY.md'

$lock = Get-Content -Raw -LiteralPath $lockPath -Encoding utf8 | ConvertFrom-Json
$notice = Get-Content -Raw -LiteralPath $noticePath -Encoding utf8

foreach ($dependency in $lock.dependencies) {
    if ([string]::IsNullOrWhiteSpace($dependency.license)) {
        throw "Dependency '$($dependency.name)' has no license recorded."
    }

    if (-not $notice.Contains($dependency.name)) {
        throw "Dependency '$($dependency.name)' is missing from NOTICE-THIRD-PARTY.md."
    }
}

Write-Output "Verified $($lock.dependencies.Count) locked dependency notice entries."

