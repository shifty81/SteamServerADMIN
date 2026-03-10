#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────
# SSA – Steam Server ADMIN  ·  Automated build script (Linux / macOS / MSYS2)
# Dependencies (GLFW, ImGui, nlohmann/json, GoogleTest) are fetched
# automatically by CMake FetchContent.  Only OpenGL dev libraries
# are required from the system.
# ──────────────────────────────────────────────────────────────
set -uo pipefail

BUILD_TYPE="${1:-Release}"

# ── 0. Set up logging ────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
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

# ── 2. Install OpenGL / X11 dev libraries if needed ──────────
install_system_deps_linux() {
    info "Detecting Linux package manager …"
    if command -v apt-get &>/dev/null; then
        info "Installing OpenGL / X11 development packages via apt …"
        sudo apt-get update -qq
        sudo apt-get install -y libgl1-mesa-dev libx11-dev \
            libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
    elif command -v dnf &>/dev/null; then
        info "Installing OpenGL / X11 development packages via dnf …"
        sudo dnf install -y mesa-libGL-devel libX11-devel \
            libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel
    elif command -v pacman &>/dev/null; then
        info "Installing OpenGL / X11 development packages via pacman …"
        sudo pacman -S --noconfirm mesa libx11 libxrandr libxinerama libxcursor libxi
    else
        err "Unsupported package manager. Please install OpenGL and X11 development libraries manually."
        return 1
    fi
}

_uname="$(uname -s)"
case "$_uname" in
    Linux)  install_system_deps_linux ;;
    Darwin) info "macOS detected — OpenGL is provided by the system frameworks." ;;
    MINGW*|MSYS*|CYGWIN*) info "Windows detected — OpenGL is provided by the graphics driver." ;;
    *) warn "Unknown OS ($_uname). Ensure OpenGL development libraries are installed." ;;
esac

# ── 3. Configure ─────────────────────────────────────────────
info "Configuring ($BUILD_TYPE) …"
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" 2>&1 | tee -a "$CONFIGURE_LOG"
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
    err "Build failed (exit code $BUILD_EXIT)."
    echo "Build failed (exit code $BUILD_EXIT)." >> "$FULL_LOG"
fi
# Only pause when running in an interactive terminal (not CI)
if [ -t 0 ] && [ -z "${CI:-}" ]; then
    printf '\n'
    read -rp "Press Enter to close …"
fi

exit "$BUILD_EXIT"
