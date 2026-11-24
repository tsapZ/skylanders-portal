#!/usr/bin/env bash
# Synthetic checks for installer/linux-setup.sh: staging, transactional promotion, preserving a
# partial rom/ and player-edited settings, rollback after a failed extraction, disc check or
# settings write (keeping the backup when the rollback itself fails), and the per-folder lock.
# Uses only throwaway directories and stub executables -- no game data.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
setup="$repo/installer/linux-setup.sh"
failures=0
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }
pass() { echo "ok: $*"; }

template() {
  cat <<'EOF'
["Portal"]
portal_mode = "__PORTAL_MODE__"
["Graphics"]
resolution = "__RESOLUTION__"
resolution_scale = __RESOLUTION_SCALE__
["Game"]
language = "__LANGUAGE__"
EOF
}

# A fresh install tree with the installer, a stub disc check and the settings template.
make_install() {
  local dir="$1"
  mkdir -p "$dir"
  cp "$setup" "$dir/linux-setup.sh"
  chmod +x "$dir/linux-setup.sh"
  template > "$dir/settings_template.toml"
  cat > "$dir/ssarecomp_xexcheck" <<'EOF'
#!/usr/bin/env bash
exit "${FAKE_XEX_EXIT:-0}"
EOF
  chmod +x "$dir/ssarecomp_xexcheck"
}

# A synthetic extracted disc folder with a default.xex and one data file.
make_disc() {
  local dir="$1"
  mkdir -p "$dir/data"
  printf 'xex' > "$dir/default.xex"
  printf 'level' > "$dir/data/level.bin"
}

# Any leftover staging/backup/temp files in an install tree? Prints them.
leftovers() {
  find "$1" -maxdepth 1 \( -name '.rom-staging.*' -o -name '.rom-backup.*' -o -name '.ssarecomp.toml.*' \) -print
}

# --- fresh install from an extracted folder ------------------------------------------------
d="$work/fresh"
make_install "$d"
make_disc "$d/disc"
bash "$d/linux-setup.sh" "$d/disc" --portal software --resolution 1280x720 --scale 3 --language french >/dev/null
if [ -f "$d/rom/default.xex" ] && [ -f "$d/rom/data/level.bin" ]; then pass "fresh install copies the disc"; else fail "fresh install copies the disc"; fi
if grep -q '^portal_mode = "software"' "$d/ssarecomp.toml" \
   && grep -q '^resolution = "1280x720"' "$d/ssarecomp.toml" \
   && grep -q '^resolution_scale = 3' "$d/ssarecomp.toml" \
   && grep -q '^language = "french"' "$d/ssarecomp.toml"; then
  pass "flags apply to the packaged default settings"
else
  fail "flags apply to the packaged default settings"
fi
[ -f "$d/disc/default.xex" ] && pass "source disc is left intact" || fail "source disc is left intact"
[ -z "$(leftovers "$d")" ] && pass "no staging or backup leftovers" || fail "no staging or backup leftovers: $(leftovers "$d")"

# --- a player-edited settings file is preserved ---------------------------------------------
d="$work/edited"
make_install "$d"
make_disc "$d/disc"
printf '["Portal"]\nportal_mode = "usb"   # mine\n' > "$d/ssarecomp.toml"
before="$(cksum < "$d/ssarecomp.toml")"
bash "$d/linux-setup.sh" "$d/disc" --portal software >/dev/null
after="$(cksum < "$d/ssarecomp.toml")"
[ "$before" = "$after" ] && pass "player-edited settings untouched" || fail "player-edited settings untouched"
[ -f "$d/rom/default.xex" ] && pass "install still succeeds beside an edited settings file" || fail "install beside edited settings"

# --- an older packaged default is treated as untouched and replaced --------------------------
d="$work/olddefault"
make_install "$d"
make_disc "$d/disc"
sed -e 's/__PORTAL_MODE__/hybrid/' -e 's/__RESOLUTION__/1920x1080/' \
    -e 's/__RESOLUTION_SCALE__/1/' -e 's/__LANGUAGE__/auto/' "$d/settings_template.toml" > "$d/ssarecomp.toml"
bash "$d/linux-setup.sh" "$d/disc" --portal software --resolution 800x600 >/dev/null
if grep -q '^portal_mode = "software"' "$d/ssarecomp.toml" && grep -q '^resolution = "800x600"' "$d/ssarecomp.toml"; then
  pass "packaged default settings are replaced by the flags"
else
  fail "packaged default settings are replaced by the flags"
fi

# --- a failed disc check keeps an existing partial rom/ and leaves no staging ----------------
d="$work/rollback-check"
make_install "$d"
make_disc "$d/disc"
mkdir -p "$d/rom"
printf 'partial' > "$d/rom/keep.me"
if FAKE_XEX_EXIT=2 bash "$d/linux-setup.sh" "$d/disc" >/dev/null 2>&1; then
  fail "a bad disc check fails the install"
else
  pass "a bad disc check fails the install"
fi
[ -f "$d/rom/keep.me" ] && pass "existing partial rom is preserved on a failed check" || fail "existing partial rom is preserved on a failed check"
[ -z "$(leftovers "$d")" ] && pass "no staging leftovers after a failed check" || fail "no staging leftovers after a failed check"

# --- a failed extraction after partial copy keeps the existing rom --------------------------
d="$work/rollback-extract"
make_install "$d"
mkdir -p "$d/rom"
printf 'partial' > "$d/rom/keep.me"
printf 'looks like an iso' > "$d/game.iso"
mkdir -p "$d/bin"
cat > "$d/bin/xdvdfs" <<'EOF'
#!/usr/bin/env bash
# Copy a little into staging, then fail, like a broken extraction.
printf 'half' > "${@: -1}/default.xex"
exit 1
EOF
chmod +x "$d/bin/xdvdfs"
if PATH="$d/bin:$PATH" bash "$d/linux-setup.sh" "$d/game.iso" >/dev/null 2>&1; then
  fail "a failed extraction fails the install"
else
  pass "a failed extraction fails the install"
fi
[ -f "$d/rom/keep.me" ] && pass "existing partial rom survives a failed extraction" || fail "existing partial rom survives a failed extraction"
[ -z "$(leftovers "$d")" ] && pass "no staging leftovers after a failed extraction" || fail "no staging leftovers after a failed extraction"

# --- a failure after the old rom is set aside restores it and drops the backup ---------------
d="$work/rollback-settings"
make_install "$d"
make_disc "$d/disc"
mkdir -p "$d/rom"
printf 'partial' > "$d/rom/keep.me"
sed -e 's/__PORTAL_MODE__/hybrid/' -e 's/__RESOLUTION__/1920x1080/' \
    -e 's/__RESOLUTION_SCALE__/1/' -e 's/__LANGUAGE__/auto/' "$d/settings_template.toml" > "$d/ssarecomp.toml"
mkdir -p "$d/bin"
cat > "$d/bin/mv" <<'EOF'
#!/usr/bin/env bash
# Fail only the settings move, like a destination that can't be replaced.
[ "${@: -1}" != "$FAIL_MV_DEST" ] || exit 1
exec /bin/mv "$@"
EOF
chmod +x "$d/bin/mv"
if PATH="$d/bin:$PATH" FAIL_MV_DEST="$d/ssarecomp.toml" bash "$d/linux-setup.sh" "$d/disc" --portal software >/dev/null 2>&1; then
  fail "a failed settings write fails the install"
else
  pass "a failed settings write fails the install"
fi
if [ -f "$d/rom/keep.me" ] && [ ! -e "$d/rom/default.xex" ]; then
  pass "a failed settings write restores the old rom"
else
  fail "a failed settings write restores the old rom"
fi
[ -z "$(leftovers "$d")" ] && pass "no backup leftovers after a restored rollback" || fail "no backup leftovers after a restored rollback: $(leftovers "$d")"

# --- a failed rollback keeps the backup and prints its path ---------------------------------
d="$work/rollback-failed"
make_install "$d"
make_disc "$d/disc"
mkdir -p "$d/rom"
printf 'partial' > "$d/rom/keep.me"
mkdir -p "$d/bin"
cat > "$d/bin/mv" <<'EOF'
#!/usr/bin/env bash
# Fail every rename into rom/, like a mount/permission error that outlives the promote attempt.
[ "${@: -1}" != "$FAIL_MV_DEST" ] || exit 1
exec /bin/mv "$@"
EOF
chmod +x "$d/bin/mv"
if PATH="$d/bin:$PATH" FAIL_MV_DEST="$d/rom" bash "$d/linux-setup.sh" "$d/disc" >/dev/null 2>"$work/rollback.err"; then
  fail "a failed promote fails the install"
else
  pass "a failed promote fails the install"
fi
backup="$(find "$d" -maxdepth 1 -name '.rom-backup.*' -print)"
if [ -n "$backup" ] && [ -f "$backup/rom/keep.me" ] && [ "$(cat "$backup/rom/keep.me")" = partial ]; then
  pass "a failed rollback keeps the old rom in its backup"
else
  fail "a failed rollback keeps the old rom in its backup"
fi
[ ! -e "$d/rom" ] && pass "a failed rollback leaves no half-restored rom" || fail "a failed rollback leaves no half-restored rom"
if grep -qF "kept in $backup/rom" "$work/rollback.err"; then
  pass "a failed rollback prints the backup path"
else
  fail "a failed rollback prints the backup path: $(cat "$work/rollback.err")"
fi

# --- two setups in one folder are serialized by a lock on the folder ------------------------
d="$work/locked"
make_install "$d"
make_disc "$d/disc"
exec 8<"$d"
flock 8
locked_status=0
timeout 2 bash "$d/linux-setup.sh" "$d/disc" >/dev/null 2>"$work/locked.err" || locked_status=$?
if [ "$locked_status" -eq 124 ]; then
  pass "a second setup waits while the folder is locked"
else
  fail "a second setup waits while the folder is locked (exit $locked_status)"
fi
grep -q "Waiting for another setup" "$work/locked.err" && pass "a waiting setup says so" || fail "a waiting setup says so"
[ ! -e "$d/rom" ] && pass "a waiting setup touches nothing" || fail "a waiting setup touches nothing"
flock -u 8
exec 8<&-
bash "$d/linux-setup.sh" "$d/disc" >/dev/null
[ -f "$d/rom/default.xex" ] && pass "the install proceeds once the lock is free" || fail "the install proceeds once the lock is free"
[ -z "$(leftovers "$d")" ] && pass "locking leaves no files behind" || fail "locking leaves no files behind: $(leftovers "$d")"

# --- a complete staging replaces a stale partial rom/ ---------------------------------------
d="$work/replace-partial"
make_install "$d"
make_disc "$d/disc"
mkdir -p "$d/rom"
printf 'stale' > "$d/rom/old.bin"
bash "$d/linux-setup.sh" "$d/disc" >/dev/null
if [ -f "$d/rom/default.xex" ] && [ ! -e "$d/rom/old.bin" ]; then
  pass "a complete staging replaces a stale partial rom"
else
  fail "a complete staging replaces a stale partial rom"
fi
[ -z "$(leftovers "$d")" ] && pass "no backup leftovers after promotion" || fail "no backup leftovers after promotion"

# --- a folder copy that misses default.xex in staging is rejected ---------------------------
d="$work/staging-copy"
make_install "$d"
make_disc "$d/disc"
mkdir -p "$d/rom"
printf 'partial' > "$d/rom/keep.me"
mkdir -p "$d/bin"
cat > "$d/bin/cp" <<'EOF'
#!/usr/bin/env bash
# Claim success but leave the staging folder without default.xex.
mkdir -p "${@: -1}"
exit 0
EOF
chmod +x "$d/bin/cp"
if PATH="$d/bin:$PATH" bash "$d/linux-setup.sh" "$d/disc" >/dev/null 2>&1; then
  fail "a folder copy without default.xex in staging is rejected"
else
  pass "a folder copy without default.xex in staging is rejected"
fi
[ -f "$d/rom/keep.me" ] && pass "the old rom survives an incomplete staging copy" || fail "the old rom survives an incomplete staging copy"
[ -z "$(leftovers "$d")" ] && pass "no staging leftovers after an incomplete copy" || fail "no staging leftovers after an incomplete copy"

# --- an interrupted copy keeps the old rom and removes its staging --------------------------
d="$work/interrupt"
make_install "$d"
make_disc "$d/disc"
mkdir -p "$d/rom"
printf 'partial' > "$d/rom/keep.me"
mkdir -p "$d/bin"
cat > "$d/bin/cp" <<'EOF'
#!/usr/bin/env bash
# Block in the copy until the setup is interrupted.
touch "$COPY_MARKER"
exec sleep 30
EOF
chmod +x "$d/bin/cp"
if command -v timeout >/dev/null 2>&1; then
  interrupted=0
  COPY_MARKER="$d/copying" PATH="$d/bin:$PATH" timeout -s TERM 2 bash "$d/linux-setup.sh" "$d/disc" >/dev/null 2>&1 || interrupted=$?
  [ -e "$d/copying" ] && pass "the interruption happens during the copy" || fail "the interruption happens during the copy"
  [ "$interrupted" -eq 124 ] && pass "an interrupted setup stops" || fail "an interrupted setup stops (exit $interrupted)"
  [ -f "$d/rom/keep.me" ] && pass "an interrupted copy keeps the old rom" || fail "an interrupted copy keeps the old rom"
  [ -z "$(leftovers "$d")" ] && pass "an interrupted copy leaves no staging" || fail "an interrupted copy leaves no staging: $(leftovers "$d")"
else
  echo "skip: timeout not installed"
fi

# --- a failed backup cleanup does not undo a completed install ------------------------------
d="$work/commit-cleanup"
make_install "$d"
make_disc "$d/disc"
mkdir -p "$d/rom"
printf 'partial' > "$d/rom/keep.me"
sed -e 's/__PORTAL_MODE__/hybrid/' -e 's/__RESOLUTION__/1920x1080/' \
    -e 's/__RESOLUTION_SCALE__/1/' -e 's/__LANGUAGE__/auto/' "$d/settings_template.toml" > "$d/ssarecomp.toml"
mkdir -p "$d/bin"
cat > "$d/bin/rm" <<'EOF'
#!/usr/bin/env bash
# Fail only the final removal of the backup directory.
case "${@: -1}" in $FAIL_RM_GLOB) exit 1 ;; esac
exec /bin/rm "$@"
EOF
chmod +x "$d/bin/rm"
PATH="$d/bin:$PATH" FAIL_RM_GLOB="$d/.rom-backup.*" bash "$d/linux-setup.sh" "$d/disc" --portal software >/dev/null 2>&1 || true
if [ -f "$d/rom/default.xex" ] && [ ! -e "$d/rom/keep.me" ]; then
  pass "a failed backup removal keeps the completed install"
else
  fail "a failed backup removal keeps the completed install"
fi
grep -q '^portal_mode = "software"' "$d/ssarecomp.toml" && pass "the chosen settings stay after a failed backup removal" || fail "the chosen settings stay after a failed backup removal"

# --- an existing install with a game is refused ---------------------------------------------
d="$work/present"
make_install "$d"
make_disc "$d/disc"
mkdir -p "$d/rom"
printf 'xex' > "$d/rom/default.xex"
if bash "$d/linux-setup.sh" "$d/disc" >/dev/null 2>&1; then
  fail "an existing game is not overwritten"
else
  pass "an existing game is not overwritten"
fi
[ -z "$(leftovers "$d")" ] && pass "no staging leftovers when refusing" || fail "no staging leftovers when refusing"

if [ "$failures" -ne 0 ]; then
  echo "$failures check(s) failed" >&2
  exit 1
fi
echo "all checks passed"
