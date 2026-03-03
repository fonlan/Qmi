param(
    [Parameter(Mandatory = $true)]
    [string]$InputPng,
    [Parameter(Mandatory = $true)]
    [string]$OutputIco
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $InputPng)) {
    throw "Input PNG not found: $InputPng"
}

Add-Type -AssemblyName System.Drawing

$iconSizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$iconImages = New-Object System.Collections.Generic.List[object]

$source = [System.Drawing.Bitmap]::new($InputPng)
try {
    foreach ($size in $iconSizes) {
        $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality

                $scale = [Math]::Min(
                    ([double]$size / [double]$source.Width),
                    ([double]$size / [double]$source.Height))
                $drawWidth = [int][Math]::Max(1.0, [Math]::Round($source.Width * $scale))
                $drawHeight = [int][Math]::Max(1.0, [Math]::Round($source.Height * $scale))
                $drawX = [int][Math]::Floor(($size - $drawWidth) / 2.0)
                $drawY = [int][Math]::Floor(($size - $drawHeight) / 2.0)

                $graphics.DrawImage(
                    $source,
                    [System.Drawing.Rectangle]::new($drawX, $drawY, $drawWidth, $drawHeight),
                    0,
                    0,
                    $source.Width,
                    $source.Height,
                    [System.Drawing.GraphicsUnit]::Pixel)
            }
            finally {
                $graphics.Dispose()
            }

            $pngStream = New-Object System.IO.MemoryStream
            try {
                $bitmap.Save($pngStream, [System.Drawing.Imaging.ImageFormat]::Png)
                $iconImages.Add([PSCustomObject]@{
                    Size = $size
                    Bytes = $pngStream.ToArray()
                })
            }
            finally {
                $pngStream.Dispose()
            }
        }
        finally {
            $bitmap.Dispose()
        }
    }
}
finally {
    $source.Dispose()
}

$outputDir = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($OutputIco))
if (-not [string]::IsNullOrWhiteSpace($outputDir) -and -not (Test-Path -LiteralPath $outputDir)) {
    [System.IO.Directory]::CreateDirectory($outputDir) | Out-Null
}

$stream = [System.IO.File]::Open($OutputIco, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
try {
    $writer = New-Object System.IO.BinaryWriter($stream)
    try {
        $entryCount = $iconImages.Count
        $imageOffset = 6 + (16 * $entryCount)

        # ICONDIR
        $writer.Write([UInt16]0) # Reserved
        $writer.Write([UInt16]1) # Type: icon
        $writer.Write([UInt16]$entryCount)

        foreach ($iconImage in $iconImages) {
            $size = [int]$iconImage.Size
            $bytes = [byte[]]$iconImage.Bytes
            $sizeByte = if ($size -ge 256) { [byte]0 } else { [byte]$size }

            # ICONDIRENTRY
            $writer.Write($sizeByte)                 # Width
            $writer.Write($sizeByte)                 # Height
            $writer.Write([Byte]0)                   # Color count
            $writer.Write([Byte]0)                   # Reserved
            $writer.Write([UInt16]1)                 # Planes
            $writer.Write([UInt16]32)                # Bit count
            $writer.Write([UInt32]$bytes.Length)     # Bytes in resource
            $writer.Write([UInt32]$imageOffset)      # Image offset

            $imageOffset += $bytes.Length
        }

        foreach ($iconImage in $iconImages) {
            $writer.Write([byte[]]$iconImage.Bytes)
        }
    }
    finally {
        $writer.Dispose()
    }
}
finally {
    $stream.Dispose()
}
