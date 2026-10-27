<#
.SYNOPSIS
Starts the game for an automated test run and takes screenshots of its window on a timeline.
Input comes from --input_script (see thirdparty/skylanders-portal/src/dev/input_script.h), so nothing here touches the desktop
focus or sends keys: a dialog in the foreground cannot break the run, and no key ends up in another
window. Screenshots come from the game window itself (PrintWindow), not from the screen.

.USAGE
    just scripted -OutDir <dir> -GameArgs "--user_data_root=<copy> --input_script=<file> ..." `
        -Shots "wait 50; shot card; wait 2; full card_big"
    just scripted ... -RenderDocCapture <path\prefix>   # start through renderdoccmd

Steps (';'-separated): wait <seconds> | shot <name> (640 wide PNG) | full <name> (full size PNG).
Always use a copied --user_data_root (docs/development.md). The game is killed after the last step
unless -KeepRunning is given (an input script can end it with quit).
#>
param(
    [Parameter(Mandatory = $true)] [string]$OutDir,
    [string]$GameArgs = "",
    [string]$Shots = "",
    [string]$Config = "release",
    [string]$RenderDocCapture = "",
    [switch]$KeepRunning
)

$ErrorActionPreference = "Stop"
# The game this runs for: the repository this library sits in (<game>\thirdparty\skylanders-portal),
# its name from the project() line of its CMakeLists.txt (e.g. giantsrecomp).
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
if (-not ((Get-Content -Raw (Join-Path $RepoRoot "CMakeLists.txt")) -match 'project\((\w+)')) { throw "no project() in $RepoRoot\CMakeLists.txt" }
$GameName = $Matches[1]
$RenderDocDir = Join-Path $RepoRoot "logs\toolchain\RenderDoc_1.46_64"
$Exe = Join-Path $RepoRoot "out\build\win-amd64-$Config\${GameName}iled.exe"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SfrWindow {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

function Get-GameWindow {
    $script:Game.Refresh()
    if ($script:Game.MainWindowHandle -eq [IntPtr]::Zero) { throw "the game has no window" }
    $script:Game.MainWindowHandle
}

function Save-Shot([string]$Name, [bool]$Full) {
    $hwnd = Get-GameWindow
    $rect = New-Object SfrWindow+RECT
    [SfrWindow]::GetClientRect($hwnd, [ref]$rect) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap ([math]::Max(1, $rect.R - $rect.L)), ([math]::Max(1, $rect.B - $rect.T))
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [SfrWindow]::PrintWindow($hwnd, $hdc, 3) | Out-Null   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc); $g.Dispose()
    $path = Join-Path $OutDir "$Name.png"
    if ($Full) {
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    } else {
        $small = New-Object System.Drawing.Bitmap $bmp, 640, ([int](640 * $bmp.Height / $bmp.Width))
        $small.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $small.Dispose()
    }
    $bmp.Dispose()
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$start = Get-Date
if ($RenderDocCapture) {
    # renderdoccmd starts the game itself, so look the process up by name.
    $rdArgs = "capture -d `"$RepoRoot`" -c `"$RenderDocCapture`" `"$Exe`" $GameArgs"
    Start-Process -FilePath (Join-Path $RenderDocDir "renderdoccmd.exe") -ArgumentList $rdArgs `
        -WorkingDirectory $RepoRoot -WindowStyle Hidden | Out-Null
    for ($i = 0; $i -lt 40 -and -not $script:Game; $i++) {
        Start-Sleep -Milliseconds 500
        $script:Game = Get-Process "${GameName}iled" -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if (-not $script:Game) { throw "the game did not start under RenderDoc" }
} elseif ($GameArgs) {
    $script:Game = Start-Process -FilePath $Exe -ArgumentList $GameArgs -WorkingDirectory $RepoRoot -PassThru
} else {
    $script:Game = Start-Process -FilePath $Exe -WorkingDirectory $RepoRoot -PassThru
}

foreach ($raw in ($Shots -split ';')) {
    $step = $raw.Trim()
    if (-not $step) { continue }
    if ($script:Game.HasExited) { "game exited early, code 0x{0:X8}" -f $script:Game.ExitCode; break }
    $words = $step -split '\s+'
    switch ($words[0]) {
        'wait' { Start-Sleep -Milliseconds ([int]([double]$words[1] * 1000)) }
        'shot' { Save-Shot $words[1] $false }
        'full' { Save-Shot $words[1] $true }
        default { throw "unknown step '$step'" }
    }
    "{0,5:N0}s  {1}" -f ((Get-Date) - $start).TotalSeconds, $step
}
if (-not $script:Game.HasExited -and -not $KeepRunning) { Stop-Process -Id $script:Game.Id -Force; "stopped the game" }
elseif ($script:Game.HasExited) { "game exited, code 0x{0:X8}" -f $script:Game.ExitCode }
