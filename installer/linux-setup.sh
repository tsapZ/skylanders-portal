#!/usr/bin/env bash
# Linux setup helper for unpacked Skylanders Recompiled releases: puts the disc files into rom/
# next to this script, checks rom/default.xex, and writes <game>.toml from settings_template.toml.
#
#   ./linux-setup.sh <game.iso | extracted-folder> [--portal hybrid|software|usb|none]
#                    [--resolution 1920x1080] [--scale 1] [--language auto]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
rom="$here/rom"
default_portal="hybrid"
default_resolution="1920x1080"
default_scale="1"
default_language="auto"
portal="$default_portal"
resolution="$default_resolution"
scale="$default_scale"
language="$default_language"
src=""
stage=""
backup=""
promoted=""
settings_tmp=""

die() { echo "error: $*" >&2; exit 1; }

# The game is copied or extracted into a fresh sibling directory. Only that directory is ever
# removed on failure; it is renamed over rom/ only once the whole copy and its version check
# passed, and an existing (partial) rom/ is kept aside until the settings file is written too, so
# a failure anywhere restores the install as it was. If that restore can't be done, the old files
# are left in their backup directory and its path is printed instead of being deleted.
cleanup() {
  local status=$?
  trap - EXIT INT TERM
  if [ -n "$settings_tmp" ]; then
    rm -f "$settings_tmp"
    settings_tmp=""
  fi
  if [ -n "$stage" ]; then
    rm -rf "$stage"
    stage=""
  fi
  if [ -n "$backup" ]; then
    if [ ! -e "$backup/rom" ]; then
      rm -rf "$backup"
    elif rm -rf "$rom" && mv -T "$backup/rom" "$rom"; then
      rm -rf "$backup"
    else
      echo "warning: couldn't restore the old $rom; the old files are kept in $backup/rom" >&2
    fi
    backup=""
  elif [ -n "$promoted" ]; then
    rm -rf "$rom"
    promoted=""
  fi
  exit "$status"
}
trap cleanup EXIT
# Turn an interruption into a normal exit so the cleanup above can put the old rom/ back.
trap 'exit 130' INT
trap 'exit 143' TERM

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

# Serialize two setups pointed at the same folder: hold a lock on this directory itself for the
# whole run, so a second run can't stage over or clean up the first run's install.
exec 9<"$here"
if ! flock -n 9; then
  echo "Waiting for another setup in $here to finish ..." >&2
  flock 9 || die "couldn't lock $here"
fi

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
    # Another build of the game, run by the exe made from it that comes with this download (skylanders-portal
    # sibling_build.h; SuperChargers' French / Dutch and Scandinavian discs, 2026-11-26).
    5) ;;
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
  stage="$(mktemp -d "$here/.rom-staging.XXXXXXXX")" || die "couldn't create a staging folder in $here"
  chmod 755 "$stage"
  echo "Copying game files into $rom ..."
  cp -r "$src_real"/. "$stage"/
  [ -f "$stage/default.xex" ] || die "default.xex didn't copy into $here/rom"
elif [ -f "$src" ]; then
  stage="$(mktemp -d "$here/.rom-staging.XXXXXXXX")" || die "couldn't create a staging folder in $here"
  chmod 755 "$stage"
  echo "Extracting $src into $rom ..."
  if command -v xdvdfs >/dev/null 2>&1; then
    xdvdfs unpack "$src" "$stage"
  elif command -v extract-xiso >/dev/null 2>&1; then
    extract-xiso -s -x "$src" -d "$stage"
  else
    die "install xdvdfs or extract-xiso to unpack an ISO, or pass an already-extracted folder"
  fi
  [ -f "$stage/default.xex" ] || { echo "default.xex not found in the ISO." >&2; false; }
  check_xex "$stage/default.xex"
else
  die "$src does not exist"
fi

render_settings() {
  sed -e "s/__PORTAL_MODE__/$1/g" \
      -e "s/__RESOLUTION__/$2/g" \
      -e "s/__RESOLUTION_SCALE__/$3/g" \
      -e "s/__LANGUAGE__/$4/g" \
      "$here/settings_template.toml"
}

# The package ships a pristine <game>.toml rendered from settings_template.toml with the default
# switches. Replace only that untouched file with the chosen --portal/--resolution/--scale/
# --language values; a player edit, or a settings file from an older package, is kept as it is.
toml="$here/${game}.toml"
prepare_settings() {
  [ -f "$here/settings_template.toml" ] || return 0
  if [ -e "$toml" ]; then
    if ! render_settings "$default_portal" "$default_resolution" "$default_scale" "$default_language" | cmp -s - "$toml"; then
      echo "Keeping your existing $toml."
      return 0
    fi
  fi
  settings_tmp="$(mktemp "$here/.${game}.toml.XXXXXXXX")" || die "couldn't write $toml"
  render_settings "$portal" "$resolution" "$scale" "$language" > "$settings_tmp" || die "couldn't write $toml"
}

prepare_settings

# Promote only after the complete game is staged and checked, keeping the old rom/ aside until the
# settings file is written too, so any failure leaves the install as it was.
if [ -e "$rom" ]; then
  backup="$(mktemp -d "$here/.rom-backup.XXXXXXXX")" || die "couldn't move the old $rom aside"
  mv -T "$rom" "$backup/rom" || die "couldn't move the old $rom aside"
fi
# Clear the staged marker only after the rename, and mark the install promoted before it, so an
# interruption can never leave a promoted rom/ behind that the cleanup doesn't know about.
promoted="1"
mv -T "$stage" "$rom" || die "couldn't move the game into $rom"
stage=""

if [ -n "$settings_tmp" ]; then
  mv -T "$settings_tmp" "$toml" || die "couldn't write $toml"
  settings_tmp=""
fi

# The install is complete: forget the rollback state before removing the backup, so a failed
# removal or an interruption can't roll back a finished install.
promoted=""
if [ -n "$backup" ]; then
  finished_backup="$backup"
  backup=""
  rm -rf "$finished_backup"
fi

echo "Done. Start the game with: cd \"$here\" && ./$(basename "$exe")"
if [ -f "$here/70-portal-of-power.rules" ] && [ "$portal" != software ] && [ "$portal" != none ]; then
  echo "For a real Portal of Power without root, install the udev rule once:"
  echo "  sudo cp \"$here/70-portal-of-power.rules\" /etc/udev/rules.d/ && sudo udevadm control --reload-rules && sudo udevadm trigger"
fi
