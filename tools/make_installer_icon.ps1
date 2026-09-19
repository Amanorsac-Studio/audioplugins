# Builds installer/assets/AmanorsacStudio.ico from the brand logo: the mark on
# the near-black brand ground, at the sizes Windows asks for. Run once, commit
# the result; the installer build only reads the .ico.
param(
    [string]$Logo = "assets/brand/AmanorsacLogo.png",
    [string]$Out = "installer/assets/AmanorsacStudio.ico"
)

Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$source = [System.Drawing.Image]::FromFile((Join-Path $root $Logo))
$sizes = 256, 128, 64, 48, 32, 24, 16
$images = @()

foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)

    $radius = [Math]::Max(2, [int]($size * 0.2))
    $d = $radius * 2
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($size - $d - 1, 0, $d, $d, 270, 90)
    $path.AddArc($size - $d - 1, $size - $d - 1, $d, $d, 0, 90)
    $path.AddArc(0, $size - $d - 1, $d, $d, 90, 90)
    $path.CloseFigure()
    $ground = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 11, 13, 16))
    $g.FillPath($ground, $path)

    # The logo is a wide wordmark. Small icons read better showing only its
    # left-hand mark, so crop to the square at the left; large ones fit it whole.
    $inset = [int]($size * 0.12)
    if ($size -ge 64) {
        $w = $size - 2 * $inset
        $h = [int]($w * $source.Height / $source.Width)
        $g.DrawImage($source, $inset, [int](($size - $h) / 2), $w, $h)
    } else {
        $side = $source.Height
        $sourceRect = New-Object System.Drawing.Rectangle 0, 0, $side, $side
        $targetRect = New-Object System.Drawing.Rectangle $inset, $inset, ($size - 2 * $inset), ($size - 2 * $inset)
        $g.DrawImage($source, $targetRect, $sourceRect, [System.Drawing.GraphicsUnit]::Pixel)
    }
    $g.Dispose()

    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $images += , @($size, $stream.ToArray())
    $bitmap.Dispose()
}

# ICO container holding PNG-compressed entries.
$outPath = Join-Path $root $Out
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
$file = [System.IO.File]::Create($outPath)
$writer = New-Object System.IO.BinaryWriter $file
$writer.Write([UInt16]0); $writer.Write([UInt16]1); $writer.Write([UInt16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $size = $image[0]; $bytes = $image[1]
    $writer.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
    $writer.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
    $writer.Write([Byte]0); $writer.Write([Byte]0)
    $writer.Write([UInt16]1); $writer.Write([UInt16]32)
    $writer.Write([UInt32]$bytes.Length); $writer.Write([UInt32]$offset)
    $offset += $bytes.Length
}
foreach ($image in $images) { $writer.Write([Byte[]]$image[1]) }
$writer.Close()
"wrote $outPath ($((Get-Item $outPath).Length) bytes, $($images.Count) sizes)"
