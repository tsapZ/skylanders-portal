#!/usr/bin/env bash
# Synthetic checks for just/game.just: the Linux arch -> triple mapping and the rejection of an
# architecture the recipes do not package, without configuring or building anything.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
recipe="$repo/just/game.just"

if ! command -v just >/dev/null 2>&1; then
  echo "skip: just not installed"
  exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cat > "$work/justfile" <<EOF
game := "ssarecomp"
repo := "SSARecomp"
import '$recipe'
show:
    @echo "{{ triple }}"
EOF

failures=0
fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }
pass() { echo "ok: $*"; }

check() {
  local arch="$1" want="$2"
  local got
  got="$(just --justfile "$work/justfile" --set host_arch "$arch" show 2>/dev/null || true)"
  if [ "$got" = "$want" ]; then
    pass "$arch -> $want"
  else
    fail "$arch -> $want (got '${got:-<error>}')"
  fi
}

check x86_64 linux-amd64
check aarch64 linux-arm64
check arm64 linux-arm64

if just --justfile "$work/justfile" --set host_arch riscv64 show >/dev/null 2>"$work/err"; then
  fail "an unsupported Linux arch is rejected"
elif grep -q "unsupported Linux architecture 'riscv64'" "$work/err"; then
  pass "an unsupported Linux arch is rejected"
else
  fail "an unsupported Linux arch is rejected with a clear message"
fi

if [ "$failures" -ne 0 ]; then
  echo "$failures check(s) failed" >&2
  exit 1
fi
echo "all checks passed"
