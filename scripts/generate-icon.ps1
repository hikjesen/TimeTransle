[CmdletBinding()]
param(
    [string] $OutputPath,
    [string] $PreviewPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path `
        $repoRoot `
        'src\controller\resources\timetransle.ico'
}
if ([string]::IsNullOrWhiteSpace($PreviewPath)) {
    $PreviewPath = Join-Path $repoRoot 'out\timetransle-icon-preview.png'
}

$svgPath = Join-Path `
    $repoRoot `
    'src\controller\resources\timetransle-icon.svg'
if (-not (Test-Path -LiteralPath $svgPath -PathType Leaf)) {
    throw "Icon source is missing: $svgPath"
}

$browserCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft\Edge\Application\msedge.exe'),
    (Join-Path $env:ProgramFiles `
        'Microsoft\Edge\Application\msedge.exe'),
    (Join-Path $env:ProgramFiles `
        'Google\Chrome\Application\chrome.exe')
)
$browser = $browserCandidates |
    Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        (Test-Path -LiteralPath $_ -PathType Leaf)
    } |
    Select-Object -First 1
if (-not $browser) {
    throw 'Microsoft Edge or Google Chrome is required to render the icon.'
}

$generationRoot = Join-Path $repoRoot 'out\icon-generation'
New-Item -ItemType Directory -Path $generationRoot -Force | Out-Null

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$rendererPath = Join-Path $generationRoot 'renderer.html'
$svgMarkup = [System.IO.File]::ReadAllText(
    $svgPath,
    [System.Text.Encoding]::UTF8)
$rendererMarkup = @"
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
html, body, svg {
    width: 100%;
    height: 100%;
    margin: 0;
    overflow: hidden;
}
</style>
</head>
<body>$svgMarkup</body>
</html>
"@
[System.IO.File]::WriteAllText(
    $rendererPath,
    $rendererMarkup,
    [System.Text.UTF8Encoding]::new($false))
$rendererUri = [System.Uri]::new($rendererPath).AbsoluteUri

$masterSize = 512
$masterPath = Join-Path $generationRoot 'timetransle-master.png'
$profilePath = Join-Path $generationRoot 'profile-master'
if (Test-Path -LiteralPath $masterPath) {
    Remove-Item -LiteralPath $masterPath -Force
}
New-Item -ItemType Directory -Path $profilePath -Force | Out-Null
& $browser `
    '--headless=new' `
    '--disable-gpu' `
    '--disable-sync' `
    '--hide-scrollbars' `
    '--no-first-run' `
    '--force-device-scale-factor=1' `
    '--default-background-color=00000000' `
    "--user-data-dir=$profilePath" `
    "--window-size=$masterSize,$masterSize" `
    "--screenshot=$masterPath" `
    $rendererUri | Out-Null
if ($LASTEXITCODE -ne 0 -or
    -not (Test-Path -LiteralPath $masterPath -PathType Leaf)) {
    throw 'Failed to render the icon master.'
}

Add-Type -AssemblyName System.Drawing
$masterImage = [System.Drawing.Image]::FromFile($masterPath)
$images = [System.Collections.Generic.List[object]]::new()
try {
    foreach ($size in $sizes) {
        $pngPath = Join-Path $generationRoot "timetransle-$size.png"
        $bitmap = [System.Drawing.Bitmap]::new(
            $size,
            $size,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.CompositingMode =
                    [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.CompositingQuality =
                    [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode =
                    [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode =
                    [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode =
                    [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage(
                    $masterImage,
                    [System.Drawing.Rectangle]::new(0, 0, $size, $size),
                    0,
                    0,
                    $masterImage.Width,
                    $masterImage.Height,
                    [System.Drawing.GraphicsUnit]::Pixel)
            }
            finally {
                $graphics.Dispose()
            }
            $bitmap.Save(
                $pngPath,
                [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $bitmap.Dispose()
        }

        $bytes = [System.IO.File]::ReadAllBytes($pngPath)
        if ($bytes.Length -lt 24 -or
            $bytes[0] -ne 0x89 -or
            $bytes[1] -ne 0x50 -or
            $bytes[2] -ne 0x4e -or
            $bytes[3] -ne 0x47) {
            throw "Rendered icon is not a PNG: $pngPath"
        }
        $width =
            ([int] $bytes[16] -shl 24) -bor
            ([int] $bytes[17] -shl 16) -bor
            ([int] $bytes[18] -shl 8) -bor
            [int] $bytes[19]
        $height =
            ([int] $bytes[20] -shl 24) -bor
            ([int] $bytes[21] -shl 16) -bor
            ([int] $bytes[22] -shl 8) -bor
            [int] $bytes[23]
        if ($width -ne $size -or $height -ne $size) {
            throw "Rendered icon has unexpected dimensions: ${width}x${height}."
        }
        $images.Add([pscustomobject] @{
            Size = $size
            Bytes = $bytes
        })
    }
}
finally {
    $masterImage.Dispose()
}

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$stream = [System.IO.File]::Create($OutputPath)
try {
    $writer = [System.IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([uint16] 0)
        $writer.Write([uint16] 1)
        $writer.Write([uint16] $images.Count)

        $offset = 6 + (16 * $images.Count)
        foreach ($image in $images) {
            $dimension = if ($image.Size -eq 256) {
                [byte] 0
            } else {
                [byte] $image.Size
            }
            $writer.Write($dimension)
            $writer.Write($dimension)
            $writer.Write([byte] 0)
            $writer.Write([byte] 0)
            $writer.Write([uint16] 1)
            $writer.Write([uint16] 32)
            $writer.Write([uint32] $image.Bytes.Length)
            $writer.Write([uint32] $offset)
            $offset += $image.Bytes.Length
        }
        foreach ($image in $images) {
            $writer.Write([byte[]] $image.Bytes)
        }
    }
    finally {
        $writer.Dispose()
    }
}
finally {
    $stream.Dispose()
}

$previewDirectory = Split-Path -Parent $PreviewPath
New-Item -ItemType Directory -Path $previewDirectory -Force | Out-Null
Copy-Item `
    -LiteralPath (Join-Path $generationRoot 'timetransle-256.png') `
    -Destination $PreviewPath `
    -Force

Write-Output "Windows icon: $OutputPath"
Write-Output "Preview: $PreviewPath"
