<#
.SYNOPSIS
Runs one of the analysis scripts in this folder on a RenderDoc capture, headless, through the
portable RenderDoc in logs\toolchain (see docs/development.md for the download). qrenderdoc owns its
command line, so the capture, the output file and the script arguments go in environment variables.

.USAGE
    just rd list    <capture.rdc> <out.txt>                    # every action + outputs
    just rd srvs    <capture.rdc> <out.txt>                    # textures read per draw
    just rd dump    <capture.rdc> <out.txt> "<dir> <eid>,<eid> [depth]"   # targets as PNG
    just rd inspect <capture.rdc> <out.txt> "<dir> <eid>,<eid>" # state + input textures
    just rd timings <capture.rdc> <out.txt>                    # GPU time per pass and action
    just rd consts  <capture.rdc> <out.txt> "<min> <v>,<v>"    # draws whose PS constants hold these floats
    just rd dumps   <capture.rdc> <out.txt>                    # EDRAM dumps and ownership transfers, timed
    just rd vpos_passes <capture.rdc> <out.txt> "<dir> [min width]"  # full-screen pixel-position passes as PNG
    just rd disasm  <capture.rdc> <out.txt> "<eid>,<eid>"      # shader code and PS constants per event
    python3 thirdparty/skylanders-portal/tools/renderdoc/group.py <list out.txt>  # list grouped by target
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("list", "srvs", "dump", "inspect", "timings", "overhead", "consts", "cbuf", "vsout", "dumps", "vpos_passes", "disasm")]
    [string]$Script,
    [Parameter(Mandatory = $true, Position = 1)] [string]$Capture,
    [Parameter(Mandatory = $true, Position = 2)] [string]$Out,
    [Parameter(Position = 3)] [string]$ScriptArgs = "",
    [int]$TimeoutSec = 600
)

$ErrorActionPreference = "Stop"
# The game: the repository this library sits in (<game>\thirdparty\skylanders-portal).
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$QRenderDoc = Join-Path $RepoRoot "logs\toolchain\RenderDoc_1.46_64\qrenderdoc.exe"
if (-not (Test-Path $QRenderDoc)) { throw "RenderDoc not found at $QRenderDoc (see docs/development.md)" }

$env:RD_DIR = $PSScriptRoot
# No __pycache__ next to the scripts (compiled files carry the local path).
$env:PYTHONDONTWRITEBYTECODE = "1"
$env:RD_CAP = (Resolve-Path $Capture).Path
$env:RD_OUT = $Out
$env:RD_ARGS = $ScriptArgs
if (Test-Path $Out) { Remove-Item $Out }
$p = Start-Process -FilePath $QRenderDoc -ArgumentList "--python `"$PSScriptRoot\$Script.py`"" `
    -WindowStyle Minimized -PassThru
if (-not $p.WaitForExit($TimeoutSec * 1000)) { Stop-Process -Id $p.Id -Force; throw "timed out" }
if (Test-Path $Out) { "wrote $Out" } else { throw "the script wrote no output (did it fail to import?)" }
