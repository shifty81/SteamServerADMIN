#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────
# SSA – Steam Server ADMIN  ·  Automated build script (Linux / macOS / MSYS2)
# ──────────────────────────────────────────────────────────────
set -uo pipefail

BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"

# ── 0. Set up logging ────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs"
mkdir -p "$LOG_DIR"

CONFIGURE_LOG="$LOG_DIR/configure.log"
BUILD_LOG="$LOG_DIR/build.log"
TEST_LOG="$LOG_DIR/test.log"
FULL_LOG="$LOG_DIR/full.log"

info()  { printf '\033[1;34m>> %s\033[0m\n' "$*"; }
warn()  { printf '\033[1;33m!! %s\033[0m\n' "$*"; }
err()   { printf '\033[1;31m!! %s\033[0m\n' "$*"; }

# Initialize every log file with a header so they are never empty
TIMESTAMP="$(date '+%Y-%m-%d %H:%M:%S')"
for _log in "$CONFIGURE_LOG" "$BUILD_LOG" "$TEST_LOG" "$FULL_LOG"; do
    printf '=== SSA Build — %s — %s ===\n' "$TIMESTAMP" "$BUILD_TYPE" > "$_log"
done

# ── Build logic (all output captured via tee) ─────────────────
build_main() {

info "Logs will be written to: $LOG_DIR"

# ── 1. Check for cmake ───────────────────────────────────────
if ! command -v cmake &>/dev/null; then
    err "CMake is not installed. Please install CMake 3.22+ and re-run this script."
    return 1
fi

# ── 2. Install Qt6 if needed ─────────────────────────────────
install_qt6_linux() {
    info "Detecting Linux package manager …"
    if command -v apt-get &>/dev/null; then
        info "Installing Qt6 development packages via apt …"
        sudo apt-get update -qq
        sudo apt-get install -y qt6-base-dev libgl1-mesa-dev
    elif command -v dnf &>/dev/null; then
        info "Installing Qt6 development packages via dnf …"
        sudo dnf install -y qt6-qtbase-devel mesa-libGL-devel
    elif command -v pacman &>/dev/null; then
        info "Installing Qt6 development packages via pacman …"
        sudo pacman -S --noconfirm qt6-base
    else
        err "Unsupported package manager. Please install Qt6 (Core, Widgets, Network) manually."
        return 1
    fi
}

install_qt6_macos() {
    if command -v brew &>/dev/null; then
        info "Installing Qt6 via Homebrew …"
        brew install qt@6
        # Make sure CMake can find the Homebrew Qt
        QT_PREFIX="$(brew --prefix qt@6)"
        export CMAKE_PREFIX_PATH="${QT_PREFIX}${CMAKE_PREFIX_PATH:+;$CMAKE_PREFIX_PATH}"
    else
        err "Homebrew not found. Please install Homebrew (https://brew.sh) or install Qt6 manually."
        return 1
    fi
}

find_qt6_windows() {
    # Search common Windows Qt6 installation paths
    local search_dirs=(
        "${QT_DIR:-}" "${Qt6_DIR:-}" "${QTDIR:-}"
        "/c/Qt" "/d/Qt"
        "${USERPROFILE:-}/Qt" "$HOME/Qt"
    )
    for base in "${search_dirs[@]}"; do
        [ -z "$base" ] || [ "$base" = "/Qt" ] && continue
        [ -d "$base" ] || continue
        # Look for versioned directories like 6.x.x/msvc*_64 or 6.x.x/mingw*_64
        for ver_dir in "$base"/6*/msvc*_64 "$base"/6*/mingw*_64; do
            if [ -f "$ver_dir/lib/cmake/Qt6/Qt6Config.cmake" ]; then
                echo "$ver_dir"
                return 0
            fi
        done
    done
    return 1
}

install_qt6_msys() {
    # 1) MSYS2 with pacman
    if command -v pacman &>/dev/null; then
        info "Installing Qt6 development packages via MSYS2 pacman …"
        if pacman -S --noconfirm "${MINGW_PACKAGE_PREFIX:-mingw-w64-x86_64}"-qt6-base; then
            return 0
        fi
        warn "MSYS2 pacman failed to install Qt6."
    fi

    # 2) Check common Windows installation paths
    local qt_path
    qt_path="$(find_qt6_windows)" && {
        info "Qt6 found at: $qt_path"
        export CMAKE_PREFIX_PATH="${qt_path}${CMAKE_PREFIX_PATH:+;$CMAKE_PREFIX_PATH}"
        return 0
    }

    # 3) Try winget / chocolatey (available from Git Bash as .exe)
    if command -v winget.exe &>/dev/null; then
        info "Attempting to install Qt via winget …"
        if winget.exe install --id=qt.qt --accept-source-agreements --accept-package-agreements 2>&1; then
            qt_path="$(find_qt6_windows)" && {
                export CMAKE_PREFIX_PATH="${qt_path}${CMAKE_PREFIX_PATH:+;$CMAKE_PREFIX_PATH}"
                return 0
            }
        fi
        warn "winget install did not succeed."
    fi

    if command -v choco.exe &>/dev/null; then
        info "Attempting to install Qt via Chocolatey …"
        if choco.exe install qt6-base-dev -y 2>&1; then
            qt_path="$(find_qt6_windows)" && {
                export CMAKE_PREFIX_PATH="${qt_path}${CMAKE_PREFIX_PATH:+;$CMAKE_PREFIX_PATH}"
                return 0
            }
        fi
        warn "Chocolatey install did not succeed."
    fi

    err "Qt6 could not be found or installed automatically."
    err "Please install Qt 6.4+ from https://www.qt.io/download and either:"
    err "  • Set CMAKE_PREFIX_PATH to the Qt6 directory, or"
    err "  • Use scripts/build.ps1 (PowerShell) which searches additional paths."
    return 1
}

check_qt6() {
    # Quick check: see if cmake can find Qt6
    local tmp
    tmp="$(mktemp -d)"
    cat > "$tmp/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.22)
project(_qt6check LANGUAGES CXX)
find_package(Qt6 QUIET COMPONENTS Core)
if(Qt6_FOUND)
    message(STATUS "Qt6_FOUND=TRUE")
else()
    message(STATUS "Qt6_FOUND=FALSE")
endif()
EOF
    local out
    out="$(cmake -S "$tmp" -B "$tmp/b" 2>&1 || true)"
    rm -rf "$tmp"
    [[ "$out" == *"Qt6_FOUND=TRUE"* ]]
}

if ! check_qt6; then
    warn "Qt6 not found — attempting automatic installation …"
    _uname="$(uname -s)"
    case "$_uname" in
        Linux)  install_qt6_linux ;;
        Darwin) install_qt6_macos ;;
        MINGW*|MSYS*|CYGWIN*) install_qt6_msys ;;
        *)
            err "Unsupported OS ($_uname). Please install Qt6 manually."
            return 1
            ;;
    esac
    # Re-check
    if ! check_qt6; then
        err "Qt6 still not found after installation attempt. Please install Qt6 (Core, Widgets, Network) manually."
        return 1
    fi
fi

info "Qt6 found ✓"

# ── 3. Configure ─────────────────────────────────────────────
info "Configuring ($BUILD_TYPE) …"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    ${CMAKE_PREFIX_PATH:+-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"} 2>&1 | tee -a "$CONFIGURE_LOG"
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    err "CMake configuration failed. See $CONFIGURE_LOG"
    return 1
fi

# ── 4. Build ─────────────────────────────────────────────────
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
info "Building with $NPROC parallel jobs …"
cmake --build "$BUILD_DIR" --parallel "$NPROC" 2>&1 | tee -a "$BUILD_LOG"
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    err "Build failed. See $BUILD_LOG"
    return 1
fi

# ── 5. Run tests (if test binary was built) ──────────────────
if [ -f "$BUILD_DIR/SSA_Tests" ]; then
    info "Running tests …"
    ctest --test-dir "$BUILD_DIR" --output-on-failure 2>&1 | tee -a "$TEST_LOG"
    if [ "${PIPESTATUS[0]}" -ne 0 ]; then
        err "Tests failed. See $TEST_LOG"
        return 1
    fi
else
    warn "Test binary not found — skipping tests."
    echo "Test binary not found — tests skipped." >> "$TEST_LOG"
fi

info "Build complete!  Binary: $BUILD_DIR/SSA"
info "Log files:"
info "  Configure : $CONFIGURE_LOG"
info "  Build     : $BUILD_LOG"
info "  Test      : $TEST_LOG"
info "  Full      : $FULL_LOG"

} # end build_main

# ── Run build_main, capturing ALL output to full.log ──────────
build_main 2>&1 | tee -a "$FULL_LOG"
BUILD_EXIT="${PIPESTATUS[0]}"

# ── Post-build: report result and pause ───────────────────────
if [ "$BUILD_EXIT" -ne 0 ]; then
    err "Build failed (exit code $BUILD_EXIT)." | tee -a "$FULL_LOG"
fi
# Only pause when running in an interactive terminal (not CI)
if [ -t 0 ] && [ -z "${CI:-}" ]; then
    printf '\n'
    read -rp "Press Enter to close …"
fi

exit "$BUILD_EXIT"
