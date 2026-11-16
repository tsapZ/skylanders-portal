#!/usr/bin/env bash
# Linux setup helper for unpacked Skylanders Recompiled releases: puts the disc files into rom/
# next to this script, checks rom/default.xex, and writes <game>.toml from settings_template.toml.
#
#   ./linux-setup.sh <game.iso | extracted-folder> [--portal hybrid|software|usb|none]
#                    [--resolution 1920x1080] [--scale 1] [--language auto]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
rom="$here/rom"
portal="hybrid"
resolution="1920x1080"
scale="1"
language="auto"
src=""

die() { echo "error: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --portal) portal="${2:-}"; shift 2 ;;
    --resolution) resolution="${2:-}"; shift 2 ;;
    --scale) scale="${2:-}"; shift 2 ;;
    --language) language="${2:-}"; shift 2 ;;
    -h|--help) sed -n 2,6p "$0"; exit 0 ;;
    *) [ -z "$src" ] || die "unexpected argument: $1"; src="$1"; shift ;;
  esac
done

[ -n "$src" ] || die "give the path to your game ISO or extracted disc folder (see --help)"
case "$portal" in hybrid|software|usb|none) ;; *) die "--portal must be hybrid, software, usb or none" ;; esac
[[ "$resolution" =~ ^[0-9]+x[0-9]+$ ]] || die "--resolution must look like 1920x1080"
[[ "$scale" =~ ^[1-8]$ ]] || die "--scale must be 1 to 8"
[[ "$language" =~ ^[A-Za-z0-9_]+$ ]] || die "--language has invalid characters"

xexcheck=""
for cand in "$here"/*_xexcheck; do
  if [ -x "$cand" ]; then
    xexcheck="$cand"
    break
  fi
done
[ -n "$xexcheck" ] || die "*_xexcheck not found next to this script"

game="$(basename "$xexcheck" _xexcheck)"
exe="$here/${game}iled"
if [ ! -x "$exe" ]; then
  for cand in "$here"/*recompiled; do
    if [ -x "$cand" ]; then
      exe="$cand"
      break
    fi
  done
fi

[ ! -e "$rom/default.xex" ] || die "$rom already has a game in it; remove it first to reinstall"

check_xex() {
  local code=0
  "$xexcheck" "$1" || code=$?
  case "$code" in
    0) ;;
    2) echo "This disc does not match the supported version." >&2; return 1 ;;
    3) echo "Couldn't read $1." >&2; return 1 ;;
    *) echo "Version check failed (exit code $code)." >&2; return 1 ;;
  esac
}

if [ -d "$src" ]; then
  [ -f "$src/default.xex" ] || die "default.xex not found in $src; is this the extracted disc?"
  check_xex "$src/default.xex"
  src_real="$(realpath "$src")"
  case "$here/" in "$src_real"/*) die "pick your original extracted folder, not one containing this install" ;; esac
  echo "Copying game files into $rom ..."
  mkdir -p "$rom"
  trap 'rm -rf "$rom"' ERR
  cp -r "$src_real"/. "$rom"/
  trap - ERR
elif [ -f "$src" ]; then
  mkdir -p "$rom"
  echo "Extracting $src into $rom ..."
  trap 'rm -rf "$rom"' ERR
  if command -v xdvdfs >/dev/null 2>&1; then
    xdvdfs unpack "$src" "$rom"
  elif command -v extract-xiso >/dev/null 2>&1; then
    extract-xiso -s -x "$src" -d "$rom"
  else
    die "install xdvdfs or extract-xiso to unpack an ISO, or pass an already-extracted folder"
  fi
  [ -f "$rom/default.xex" ] || { echo "default.xex not found in the ISO." >&2; false; }
  check_xex "$rom/default.xex"
  trap - ERR
else
  die "$src does not exist"
fi

# Never overwrite an existing settings file that the player may already have customized.
toml="$here/${game}.toml"
if [ ! -e "$toml" ] && [ -f "$here/settings_template.toml" ]; then
  sed -e "s/__PORTAL_MODE__/$portal/g" \
      -e "s/__RESOLUTION__/$resolution/g" \
      -e "s/__RESOLUTION_SCALE__/$scale/g" \
      -e "s/__LANGUAGE__/$language/g" \
      "$here/settings_template.toml" > "$toml"
fi

echo "Done. Start the game with: cd \"$here\" && ./$(basename "$exe")"
if [ -f "$here/70-portal-of-power.rules" ] && [ "$portal" != software ] && [ "$portal" != none ]; then
  echo "For a real Portal of Power without root, install the udev rule once:"
  echo "  sudo cp \"$here/70-portal-of-power.rules\" /etc/udev/rules.d/ && sudo udevadm control --reload-rules && sudo udevadm trigger"
fi
