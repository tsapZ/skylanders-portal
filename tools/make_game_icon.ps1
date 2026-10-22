<#
.SYNOPSIS
Makes a game's exe icon from the owner's disc (cmake/game_icon.cmake runs it at build time).

The picture is the game's NXE dashboard tile: nxeslot.jpg (420x320, the game's logo on its
background, "SKYLANDERS SPYRO'S ADVENTURE" and so on) in the disc's `nxeart`, an STFS package
(PIRS). It is fitted into a square, transparent above and below, and written as an .ico with
256, 64, 48, 32 and 16 px PNG images. Without the disc's package, or if it can't be read, the icon
is a plain one of our own (no game art). The picture only ends up in the build folder and the exe
built there, never in a repository.

.USAGE
    make_game_icon.ps1 -Rom <the game's rom folder> -Out <file.ico>
#>
param(
    [Parameter(Mandatory = $true)] [string]$Rom,
    [Parameter(Mandatory = $true)] [string]$Out
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Get-U32BE([byte[]]$d, [int]$o) {
    return ([uint32]$d[$o] -shl 24) -bor ([uint32]$d[$o + 1] -shl 16) -bor ([uint32]$d[$o + 2] -shl 8) -bor [uint32]$d[$o + 3]
}
function Get-U24LE([byte[]]$d, [int]$o) {
    return [int]$d[$o] -bor ([int]$d[$o + 1] -shl 8) -bor ([int]$d[$o + 2] -shl 16)
}

# One file of an STFS package from a disc (its blocks written one after the other), or $null.
function Read-StfsFile([byte[]]$d, [string]$name) {
    $magic = [System.Text.Encoding]::ASCII.GetString($d, 0, 4)
    if ($magic -ne "PIRS" -and $magic -ne "LIVE" -and $magic -ne "CON ") { return $null }
    $headerSize = Get-U32BE $d 0x340
    $firstBlock = ($headerSize + 0xFFF) -band (-bnot 0xFFF)
    # The volume descriptor: block separation, the file table's block count and first block.
    $blockSep = $d[0x37B]
    $tableBlocks = [int]$d[0x37C] -bor ([int]$d[0x37D] -shl 8)
    $tableFirst = Get-U24LE $d 0x37E
    # A hash table every 170 blocks (and every 28900), one per level in a package read-only from
    # the start (block separation bit 0), else two.
    $tables = if ($blockSep -band 1) { 1 } else { 2 }
    $offsetOf = {
        param([int]$block)
        $base = ([math]::Floor($block / 170) + 1) * $tables
        if ($block -ge 170) {
            $base += ([math]::Floor($block / 28900) + 1) * $tables
            if ($block -ge 28900) { $base += $tables }
        }
        return [int64]$firstBlock + [int64]($block + $base) * 0x1000
    }
    for ($b = 0; $b -lt $tableBlocks; $b++) {
        $table = & $offsetOf ($tableFirst + $b)
        for ($i = 0; $i -lt 64; $i++) {
            $e = $table + $i * 0x40
            if ($e + 0x40 -gt $d.Length) { break }
            $len = $d[$e + 0x28] -band 0x3F
            if ($len -eq 0) { continue }
            $entryName = [System.Text.Encoding]::ASCII.GetString($d, $e, $len)
            if ($entryName -ne $name) { continue }
            $start = Get-U24LE $d ($e + 0x2F)
            $size = Get-U32BE $d ($e + 0x34)
            $out = New-Object byte[] $size
            $done = 0
            for ($block = $start; $done -lt $size; $block++) {
                $at = & $offsetOf $block
                $n = [math]::Min(0x1000, $size - $done)
                if ($at + $n -gt $d.Length) { return $null }
                [Array]::Copy($d, $at, $out, $done, $n)
                $done += $n
            }
            return , $out
        }
    }
    return $null
}

# The picture fitted into a square of `side` pixels, as PNG bytes.
function Get-SquarePng($picture, [int]$side) {
    $bmp = New-Object System.Drawing.Bitmap $side, $side, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    if ($picture) {
        $scale = $side / [math]::Max($picture.Width, $picture.Height)
        $w = [math]::Max(1, [int][math]::Round($picture.Width * $scale))
        $h = [math]::Max(1, [int][math]::Round($picture.Height * $scale))
        $g.DrawImage($picture, [int](($side - $w) / 2), [int](($side - $h) / 2), $w, $h)
    } else {
        # Our own plain icon: a dark blue disc with a golden ring.
        $m = [math]::Max(1, [int]($side / 16))
        $blue = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 20, 30, 80))
        $g.FillEllipse($blue, $m, $m, $side - 2 * $m - 1, $side - 2 * $m - 1)
        $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 255, 204, 64)), ([math]::Max(1, $side / 10))
        $r = [int]($side * 0.28)
        $g.DrawEllipse($pen, [int]($side / 2 - $r), [int]($side / 2 - $r), 2 * $r, 2 * $r)
        $blue.Dispose(); $pen.Dispose()
    }
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return , $ms.ToArray()
}

$picture = $null
$package = Join-Path $Rom "nxeart"
if (Test-Path $package) {
    try {
        $jpg = Read-StfsFile ([System.IO.File]::ReadAllBytes($package)) "nxeslot.jpg"
        if ($jpg) { $picture = [System.Drawing.Image]::FromStream((New-Object System.IO.MemoryStream (, $jpg))) }
    } catch {
        $picture = $null
    }
}
if ($picture) {
    Write-Output "game icon: the disc's dashboard tile ($($picture.Width)x$($picture.Height))"
} else {
    Write-Output "game icon: no nxeart in $Rom, a plain icon"
}

# The .ico: a header, one 16-byte entry per size, then the PNGs.
$sizes = @(256, 64, 48, 32, 16)
$images = @()
foreach ($s in $sizes) { $images += , (Get-SquarePng $picture $s) }
$ico = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $ico
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $side = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $w.Write([byte]$side); $w.Write([byte]$side); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$images[$i].Length); $w.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $w.Write($img) }
$w.Flush()
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
[System.IO.File]::WriteAllBytes($Out, $ico.ToArray())
if ($picture) { $picture.Dispose() }
