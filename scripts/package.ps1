[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',

    [ValidatePattern('^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$')]
    [string] $Version = '0.1.5',

    [switch] $SkipBuild,

    [switch] $RequireSigned
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$configurationName = $Configuration.ToLowerInvariant()
$packageRoot = Join-Path $repoRoot 'out\package'
$packageName = "TimeTransle-$Version-win-x64-portable"
$stageRoot = Join-Path $packageRoot $packageName
$zipPath = Join-Path $packageRoot "$packageName.zip"
$x64Bin = Join-Path $repoRoot "out\build\windows-x64\bin\$Configuration"
$x86Bin = Join-Path $repoRoot "out\build\windows-x86\bin\$Configuration"
$uiRoot = Join-Path $repoRoot 'ui'
$uiDist = Join-Path $uiRoot 'dist'

function Write-Utf8File {
    param(
        [Parameter(Mandatory)]
        [string] $Path,

        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [AllowEmptyString()]
        [string[]] $Lines
    )

    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllLines($Path, $Lines, $encoding)
}

function Assert-File {
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required package input is missing: $Path"
    }
}

function Copy-PackageFile {
    param(
        [Parameter(Mandatory)]
        [string] $Source,

        [Parameter(Mandatory)]
        [string] $Destination
    )

    Assert-File -Path $Source
    $destinationDirectory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

if (-not $SkipBuild) {
    Push-Location $uiRoot
    try {
        & npm.cmd ci
        if ($LASTEXITCODE -ne 0) {
            throw 'npm ci failed.'
        }
        & npm.cmd run check
        if ($LASTEXITCODE -ne 0) {
            throw 'UI validation failed.'
        }
    }
    finally {
        Pop-Location
    }

    & powershell.exe `
        -NoProfile `
        -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot 'build-all.ps1') `
        -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw 'Native build failed.'
    }
}

& powershell.exe `
    -NoProfile `
    -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot 'verify-licenses.ps1')
if ($LASTEXITCODE -ne 0) {
    throw 'Dependency notice verification failed.'
}

foreach ($required in @(
        (Join-Path $x64Bin 'TimeTransleController.exe'),
        (Join-Path $x64Bin 'TimeTransleInjector64.exe'),
        (Join-Path $x64Bin 'TimeTransleHook64.dll'),
        (Join-Path $x86Bin 'TimeTransleInjector32.exe'),
        (Join-Path $x86Bin 'TimeTransleHook32.dll'),
        (Join-Path $uiDist 'index.html')
    )) {
    Assert-File -Path $required
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
$resolvedPackageRoot = (Resolve-Path -LiteralPath $packageRoot).Path
if (-not $stageRoot.StartsWith(
        $resolvedPackageRoot + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to replace a package directory outside out\package.'
}
if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
foreach ($artifact in @($zipPath)) {
    if (Test-Path -LiteralPath $artifact) {
        Remove-Item -LiteralPath $artifact -Force
    }
}

New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
Copy-PackageFile `
    -Source (Join-Path $x64Bin 'TimeTransleController.exe') `
    -Destination (Join-Path $stageRoot 'TimeTransleController.exe')
Copy-PackageFile `
    -Source (Join-Path $x64Bin 'TimeTransleInjector64.exe') `
    -Destination (Join-Path $stageRoot 'TimeTransleInjector64.exe')
Copy-PackageFile `
    -Source (Join-Path $x64Bin 'TimeTransleHook64.dll') `
    -Destination (Join-Path $stageRoot 'TimeTransleHook64.dll')
Copy-PackageFile `
    -Source (Join-Path $x86Bin 'TimeTransleInjector32.exe') `
    -Destination (Join-Path $stageRoot 'TimeTransleInjector32.exe')
Copy-PackageFile `
    -Source (Join-Path $x86Bin 'TimeTransleHook32.dll') `
    -Destination (Join-Path $stageRoot 'TimeTransleHook32.dll')

$resourceDestination = Join-Path $stageRoot 'resources\ui'
New-Item -ItemType Directory -Path $resourceDestination -Force | Out-Null
Copy-Item `
    -Path (Join-Path $uiDist '*') `
    -Destination $resourceDestination `
    -Recurse `
    -Force

$licenseRoot = Join-Path $stageRoot 'licenses'
$nativeLicenseRoot = Join-Path $licenseRoot 'native'
$npmLicenseRoot = Join-Path $licenseRoot 'npm'
New-Item -ItemType Directory -Path $nativeLicenseRoot -Force | Out-Null
New-Item -ItemType Directory -Path $npmLicenseRoot -Force | Out-Null

$nativeLicenses = @(
    @{
        Name = 'MinHook'
        Version = '1.3.4'
        License = 'BSD-2-Clause'
        Files = @(
            (Join-Path $repoRoot 'out\build\windows-x64\_deps\minhook-src\LICENSE.txt')
        )
    },
    @{
        Name = 'nlohmann-json'
        Version = '3.12.0'
        License = 'MIT'
        Files = @(
            (Join-Path $repoRoot 'out\build\windows-x64\_deps\nlohmann_json-src\LICENSE.MIT')
        )
    },
    @{
        Name = 'Microsoft-WebView2-SDK'
        Version = '1.0.3856.49'
        License = 'Microsoft.WebView2'
        Files = @(
            (Join-Path $repoRoot 'out\build\windows-x64\_deps\webview2-src\LICENSE.txt'),
            (Join-Path $repoRoot 'out\build\windows-x64\_deps\webview2-src\NOTICE.txt')
        )
    }
)

$noticeRows = [System.Collections.Generic.List[string]]::new()
foreach ($dependency in $nativeLicenses) {
    $copiedNames = [System.Collections.Generic.List[string]]::new()
    foreach ($licenseFile in $dependency.Files) {
        Assert-File -Path $licenseFile
        $leaf = Split-Path -Leaf $licenseFile
        $destinationName = "$($dependency.Name)-$leaf"
        Copy-PackageFile `
            -Source $licenseFile `
            -Destination (Join-Path $nativeLicenseRoot $destinationName)
        $copiedNames.Add("licenses/native/$destinationName")
    }
    $noticeRows.Add(
        "$($dependency.Name) $($dependency.Version) | " +
        "$($dependency.License) | " +
        ($copiedNames -join ', '))
}

Push-Location $uiRoot
try {
    $npmPaths = @(& npm.cmd ls --omit=dev --all --parseable)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to enumerate installed production npm dependencies.'
    }
}
finally {
    Pop-Location
}

$seenPackages = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($npmPath in $npmPaths) {
    if ([string]::IsNullOrWhiteSpace($npmPath) -or
        $npmPath -eq $uiRoot) {
        continue
    }
    $packageJsonPath = Join-Path $npmPath 'package.json'
    Assert-File -Path $packageJsonPath
    $packageJson = Get-Content `
        -Raw `
        -LiteralPath $packageJsonPath `
        -Encoding utf8 |
        ConvertFrom-Json
    $identity = "$($packageJson.name)@$($packageJson.version)"
    if (-not $seenPackages.Add($identity)) {
        continue
    }

    $licenseFiles = @(
        Get-ChildItem -LiteralPath $npmPath -File |
        Where-Object {
            $_.Name -match '^(LICENSE|LICENCE|COPYING|NOTICE)(?:[._-].*)?$'
        } |
        Sort-Object Name
    )
    if ($licenseFiles.Count -eq 0) {
        throw "Production npm dependency '$identity' has no license text."
    }

    $safeIdentity = $identity -replace '[^0-9A-Za-z._-]', '_'
    $copiedNames = [System.Collections.Generic.List[string]]::new()
    foreach ($licenseFile in $licenseFiles) {
        $safeLeaf = $licenseFile.Name -replace '[^0-9A-Za-z._-]', '_'
        $destinationName = "$safeIdentity-$safeLeaf"
        Copy-PackageFile `
            -Source $licenseFile.FullName `
            -Destination (Join-Path $npmLicenseRoot $destinationName)
        $copiedNames.Add("licenses/npm/$destinationName")
    }
    $licenseId = if ($packageJson.license) {
        [string] $packageJson.license
    } else {
        'SEE-LICENSE-FILES'
    }
    $noticeRows.Add(
        "$identity | $licenseId | " +
        ($copiedNames -join ', '))
}

$noticeLines = @(
    'TimeTransle third-party notices',
    '',
    'This portable package contains the following runtime dependencies.',
    'Exact license and notice texts are stored at the listed relative paths.',
    ''
) + ($noticeRows | Sort-Object)
Write-Utf8File `
    -Path (Join-Path $stageRoot 'THIRD-PARTY-NOTICES.txt') `
    -Lines $noticeLines

$portableReadme = @(
    'TimeTransle portable package',
    '',
    'Run TimeTransleController.exe. Keep all EXE, DLL, resources and licenses',
    'in this directory together.',
    '',
    'Supported host: x64 Windows 10 or Windows 11.',
    'Supported targets: user-selected running x86/WOW64 and x64 desktop processes.',
    'Microsoft Edge WebView2 Evergreen Runtime is required.',
    'If it is missing, TimeTransle offers to open the official Microsoft download page.',
    '',
    'Global shortcuts:',
    '  Insert+F10: temporarily use 1x speed.',
    '  Insert+F11: restore the previous non-1x speed.',
    '  Insert+F12: show and focus the TimeTransle window.',
    '',
    'This build uses standard LoadLibraryW + CreateRemoteThread loading only.',
    'It does not use hidden loading, manual mapping, drivers or protection bypasses.',
    'Documented hard safety gates fail closed; other failures report actual system',
    'access, DLL loading or Hook initialization results.'
)
Write-Utf8File `
    -Path (Join-Path $stageRoot 'README.txt') `
    -Lines $portableReadme

$signatureLines = [System.Collections.Generic.List[string]]::new()
foreach ($binary in Get-ChildItem -LiteralPath $stageRoot -File |
    Where-Object { $_.Extension -in @('.exe', '.dll') } |
    Sort-Object Name) {
    $signature = Get-AuthenticodeSignature -LiteralPath $binary.FullName
    $signatureLines.Add("$($binary.Name) | $($signature.Status)")
    if ($RequireSigned -and $signature.Status -ne 'Valid') {
        throw "Required Authenticode signature is not valid: $($binary.Name)"
    }
}
Write-Utf8File `
    -Path (Join-Path $stageRoot 'SIGNATURE-STATUS.txt') `
    -Lines $signatureLines

Compress-Archive `
    -Path (Join-Path $stageRoot '*') `
    -DestinationPath $zipPath `
    -CompressionLevel Optimal
Write-Output "Portable package: $zipPath"
Write-Output "Signature policy: $(if ($RequireSigned) { 'required' } else { 'recorded' })"
