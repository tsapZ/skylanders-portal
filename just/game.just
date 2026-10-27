# The six games' dev recipes, imported by each game's justfile, which sets `game` (the CMake
# project and file prefix, e.g. ssarecomp) and `repo` (the repository name, e.g. SSARecomp).
# Recipes run in the game's folder; `just` there lists them.
#
# Needs: CMake, Ninja, `just`, and clang-20/clang++-20 on PATH.
#   - Windows: Visual Studio 2022 with the "C++ Clang tools for Windows" component (its bundled
#     clang is put on PATH below), plus Git for Windows (its own usr/bin isn't on PATH by default,
#     so it's pointed at directly below -- these recipes run under its sh.exe). Works from any
#     shell (PowerShell, cmd, Git Bash); the VS "x64 Native Tools" prompt is not required.
#   - Linux: clang-20/clang++-20 on PATH. Some distros (e.g. Arch's `clang20` package) install them
#     unversioned under a version-specific directory (e.g. /usr/lib/llvm20/bin/clang++, no
#     "-20" suffix) -- add that directory to PATH, or symlink clang-20/clang++-20 onto it yourself.
#   - macOS: Xcode's clang normally suffices.

set windows-shell := ["C:/Program Files/Git/usr/bin/sh.exe", "-cu"]

# On Windows, `just` is a native program, so it always sees PATH in Windows form (';'-separated),
# even when started from Git Bash; append in the same form, and sh.exe converts the whole value.
# rexruntime.dll imports amd_fidelityfx_dx12.dll (debug: amd_fidelityfx_dx12d.dll). The SDK builds it into
# bin/ (git-ignored), but the codegen tool (thirdparty/rexglue-sdk/out/win-amd64/rexglue[d].exe) does not
# depend on it, so the regen recipes build it first and PATH points there. Without it codegen
# exits with 0xC0000135 (DLL not found).
ffx_dll_dirs := justfile_directory() + "\\bin"

export PATH := if os() == "windows" {
    env_var('PATH') + ";C:\\Program Files\\Git\\usr\\bin;C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\Llvm\\x64\\bin;" + ffx_dll_dirs
} else {
    env_var('PATH')
}

# The game's executable, without the .exe suffix.
exe := game + "iled"

# Preset/output-directory triple for the host OS -- matches the *-amd64-* preset names in
# CMakePresets.json (win-amd64, linux-amd64, mac-amd64). Not parameterized for arm64: neither was
# the original Windows-only version of this file.
triple := if os() == "windows" { "win-amd64" } else if os() == "macos" { "mac-amd64" } else { "linux-amd64" }

# Host executable suffix: ".exe" on Windows, empty everywhere else.
exe_suffix := if os() == "windows" { ".exe" } else { "" }

# Where the SDK puts its own shared libraries (librexruntime, libTracyClient, ...), matching
# rexglue-sdk/CMakeLists.txt's CMAKE_RUNTIME_OUTPUT_DIRECTORY. On Windows and macOS, the SDK's
# rexglue_configure_target() copies these next to the host executable as a post-build step, so
# out/build/<triple>-*/ already has everything it needs. On Linux it only stages the GPU plugin
# that way, not the runtime libraries themselves -- so play-*/package-release point
# LD_LIBRARY_PATH/DYLD_LIBRARY_PATH at this directory instead of relying on a copy that doesn't
# happen. Harmless to set on every platform.
sdk_lib_dir := "thirdparty/rexglue-sdk/out/" + triple

# List every recipe
default:
    @just --list

# --- Build -------------------------------------------------------------------

# Configure the debug preset from scratch. Not normally needed by hand -- build-debug does this
# automatically the first time (when out/build/<triple>-debug doesn't exist yet).
configure-debug:
    cmake --preset {{ triple }}-debug

configure-release:
    cmake --preset {{ triple }}-release

# Configure (if not already configured) and build the debug preset.
# Note: recipe lines below without a shebang each run as their own separate shell invocation (no
# shared state) -- just itself still stops the recipe if any line fails, so that's fine as long as
# no line depends on a variable set by an earlier one. Recipes that need that (package-release,
# logs-*) chain their lines with && on one shell line instead.
build-debug:
    if [ ! -f out/build/{{ triple }}-debug/build.ninja ]; then just configure-debug; fi
    cmake --build --preset {{ triple }}-debug

# Configure (if not already configured) and build the release preset.
build-release:
    if [ ! -f out/build/{{ triple }}-release/build.ninja ]; then just configure-release; fi
    cmake --build --preset {{ triple }}-release

# Regenerate recompiled code from rom/default.xex, then reconfigure and rebuild (debug). Run this
# after replacing rom/default.xex with a different dump -- see README.md's "For developers".
regen-debug:
    if [ ! -f out/build/{{ triple }}-debug/build.ninja ]; then just configure-debug; fi
    if [ "{{ os() }}" = "windows" ]; then cmake --build --preset {{ triple }}-debug --target amd_fidelityfx_dx12; fi
    cmake --build --preset {{ triple }}-debug --target {{ game }}_codegen
    cmake --preset {{ triple }}-debug
    cmake --build --preset {{ triple }}-debug

regen-release:
    if [ ! -f out/build/{{ triple }}-release/build.ninja ]; then just configure-release; fi
    if [ "{{ os() }}" = "windows" ]; then cmake --build --preset {{ triple }}-release --target amd_fidelityfx_dx12; fi
    cmake --build --preset {{ triple }}-release --target {{ game }}_codegen
    cmake --preset {{ triple }}-release
    cmake --build --preset {{ triple }}-release

# --- Play ----------------------------------------------------------------------

# Build (only if needed -- ninja no-ops when nothing changed) then run the debug build. Extra
# args are passed straight to the game, e.g.:
#   just play-debug --portal_mode usb
play-debug *ARGS: build-debug
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:{{ sdk_lib_dir }}" DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-}:{{ sdk_lib_dir }}" ./out/build/{{ triple }}-debug/{{ exe }}{{ exe_suffix }} {{ ARGS }}

# Build (only if needed) then run the release build -- smoother than debug, prefer this for
# actually playing rather than debugging.
play-release *ARGS: build-release
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:{{ sdk_lib_dir }}" DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-}:{{ sdk_lib_dir }}" ./out/build/{{ triple }}-release/{{ exe }}{{ exe_suffix }} {{ ARGS }}

# --- Package -------------------------------------------------------------------

# Package the release build for copying to another machine: the executable, its runtime shared
# libraries, and the settings template -- everything except rom/. This repo ships no game data on
# purpose (see README.md), and the running game reads from rom/ continuously, not just at build
# time, so your own disc dump (rom/default.xex, same version 1.0 USA/EU, plus the rest of the
# extracted disc) has to exist on the target machine too -- bring it over separately and drop it in
# next to the packaged executable there.
package-release: build-release
    dist=dist/{{ game }}-{{ triple }}-release && \
    rm -rf "$dist" && \
    mkdir -p "$dist" && \
    cp out/build/{{ triple }}-release/{{ exe }}{{ exe_suffix }} "$dist/" && \
    for lib in out/build/{{ triple }}-release/*.dll out/build/{{ triple }}-release/*.so out/build/{{ triple }}-release/*.dylib {{ sdk_lib_dir }}/*.so {{ sdk_lib_dir }}/*.dylib; do [ -e "$lib" ] && cp "$lib" "$dist/"; done && \
    cp {{ game }}.toml.example "$dist/{{ game }}.toml" && \
    echo "Packaged to $dist/" && \
    echo "Copy that folder to the other machine, then add your own rom/ next to {{ exe }}{{ exe_suffix }} there" && \
    echo "(rom/default.xex must exist) before running it."

# Build the release binary, stage it alongside the installer's other inputs, and compile the
# Windows installer with Inno Setup (ISCC.exe). Windows only -- see docs/releasing.md. Checks both
# common Inno Setup 6 install locations (Program Files, from its official installer running
# elevated, and the per-user AppData location winget can use instead).
package-installer: build-release
    rm -rf installer/staging && \
    mkdir -p installer/staging && \
    cp out/build/win-amd64-release/{{ exe }}.exe installer/staging/ && \
    cp out/build/win-amd64-release/{{ game }}_xexcheck.exe installer/staging/ && \
    for lib in out/build/win-amd64-release/*.dll; do [ -e "$lib" ] && cp "$lib" installer/staging/; done && \
    iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe" && \
    [ -e "$iscc" ] || iscc="$USERPROFILE/AppData/Local/Programs/Inno Setup 6/ISCC.exe" && \
    [ -e "$iscc" ] || (echo "ISCC.exe not found -- install Inno Setup 6 (winget install JRSoftware.InnoSetup)" && exit 1) && \
    "$iscc" installer/{{ repo }}.iss && \
    echo "Installer built in installer/Output/"

# --- Test ------------------------------------------------------------------

test-debug: build-debug
    ctest --test-dir out/build/{{ triple }}-debug --output-on-failure

test-release: build-release
    ctest --test-dir out/build/{{ triple }}-release --output-on-failure

# --- Housekeeping --------------------------------------------------------------

# Delete a preset's entire build output (forces a full reconfigure + rebuild next time you build).
clean-debug:
    rm -rf out/build/{{ triple }}-debug

clean-release:
    rm -rf out/build/{{ triple }}-release

clean: clean-debug clean-release

# Show the newest debug-build log file. Extra args go straight to `tail`, e.g.:
#   just logs-debug -f        (follow it live)
#   just logs-debug -n 200    (last 200 lines instead of the default 10)
logs-debug *ARGS:
    f=$(ls -t out/build/{{ triple }}-debug/logs/*.log | head -1) && \
    echo "==> $f" && \
    tail {{ ARGS }} "$f"

logs-release *ARGS:
    f=$(ls -t out/build/{{ triple }}-release/logs/*.log | head -1) && \
    echo "==> $f" && \
    tail {{ ARGS }} "$f"

# --- Test runs and analysis --------------------------------------------------------

# Start the release build for a scripted test run with window shots on a timeline (see
# thirdparty/skylanders-portal/tools/run_scripted.ps1), e.g.:
#   just scripted -OutDir logs/shots -GameArgs "--user_data_root=<copy> --input_script=<file>" -Shots "wait 50; full a"
scripted *ARGS:
    powershell -NoProfile -ExecutionPolicy Bypass -File thirdparty/skylanders-portal/tools/run_scripted.ps1 {{ ARGS }}

# Ghidra on rom/default.xex: setup, import, search, dump <address>, rename (see
# docs/reverse-engineering.md).
ghidra *ARGS:
    powershell -NoProfile -ExecutionPolicy Bypass -File thirdparty/skylanders-portal/tools/ghidra_re.ps1 {{ ARGS }}

# RenderDoc capture analysis, headless (thirdparty/skylanders-portal/tools/renderdoc/rd.ps1).
rd *ARGS:
    powershell -NoProfile -ExecutionPolicy Bypass -File thirdparty/skylanders-portal/tools/renderdoc/rd.ps1 {{ ARGS }}
