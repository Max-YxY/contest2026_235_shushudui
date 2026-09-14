param(
  [Parameter(Mandatory = $true)] [string] $ImagePath,
  [Parameter(Mandatory = $true)] [string] $OutputPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$width = 1024
$height = 600
$payloadBytes = $width * $height * 2
$magic = [Text.Encoding]::ASCII.GetBytes('OVIP4IMG')
$headerBytes = 64

$source = [Drawing.Image]::FromFile((Resolve-Path -LiteralPath $ImagePath))
try {
  $canvas = New-Object Drawing.Bitmap $width, $height, ([Drawing.Imaging.PixelFormat]::Format24bppRgb)
  try {
    $graphics = [Drawing.Graphics]::FromImage($canvas)
    try {
      $graphics.Clear([Drawing.Color]::Black)
      $scale = [Math]::Min($width / $source.Width, $height / $source.Height)
      $drawWidth = [int][Math]::Round($source.Width * $scale)
      $drawHeight = [int][Math]::Round($source.Height * $scale)
      $drawX = [int](($width - $drawWidth) / 2)
      $drawY = [int](($height - $drawHeight) / 2)
      $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
      $graphics.DrawImage($source, $drawX, $drawY, $drawWidth, $drawHeight)
    } finally { $graphics.Dispose() }

    $payload = New-Object byte[] $payloadBytes
    $rect = New-Object Drawing.Rectangle 0, 0, $width, $height
    $locked = $canvas.LockBits($rect, [Drawing.Imaging.ImageLockMode]::ReadOnly, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
      $row = New-Object byte[] ([Math]::Abs($locked.Stride))
      for ($y = 0; $y -lt $height; $y++) {
        [Runtime.InteropServices.Marshal]::Copy([IntPtr]($locked.Scan0.ToInt64() + $y * $locked.Stride), $row, 0, $row.Length)
        for ($x = 0; $x -lt $width; $x++) {
          $base = $x * 3
          $b = $row[$base]
          $g = $row[$base + 1]
          $r = $row[$base + 2]
          $pixel = (($r -band 0xF8) -shl 8) -bor (($g -band 0xFC) -shl 3) -bor ($b -shr 3)
          $index = ($y * $width + $x) * 2
          $payload[$index] = $pixel -band 0xFF
          $payload[$index + 1] = ($pixel -shr 8) -band 0xFF
        }
      }
    } finally { $canvas.UnlockBits($locked) }
  } finally { $canvas.Dispose() }
} finally { $source.Dispose() }

$hash = [Security.Cryptography.SHA256]::Create().ComputeHash($payload)
$header = New-Object byte[] $headerBytes
[Array]::Copy($magic, 0, $header, 0, $magic.Length)
[Array]::Copy([BitConverter]::GetBytes([uint32]1), 0, $header, 8, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]$headerBytes), 0, $header, 12, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]$width), 0, $header, 16, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]$height), 0, $header, 20, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]1), 0, $header, 24, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]$payloadBytes), 0, $header, 28, 4)
[Array]::Copy($hash, 0, $header, 32, $hash.Length)

$package = New-Object byte[] ($headerBytes + $payloadBytes)
[Array]::Copy($header, 0, $package, 0, $headerBytes)
[Array]::Copy($payload, 0, $package, $headerBytes, $payloadBytes)
[IO.File]::WriteAllBytes($OutputPath, $package)

[pscustomobject]@{
  Package = (Resolve-Path -LiteralPath $OutputPath).Path
  Bytes = $package.Length
  Canvas = "$width`x$height RGB565 little-endian"
  PayloadSha256 = ([BitConverter]::ToString($hash).Replace('-', '').ToLowerInvariant())
  PackageSha256 = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLowerInvariant()
} | Format-List
