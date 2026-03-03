#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────
# SSA – Steam Server ADMIN  ·  Automated build script (Linux / macOS)
# ──────────────────────────────────────────────────────────────
set -euo pipefail

BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"

info()  { printf '\033[1;34m>> %s\033[0m\n' "$*"; }
warn()  { printf '\033[1;33m!! %s\033[0m\n' "$*"; }
err()   { printf '\033[1;31m!! %s\033[0m\n' "$*"; exit 1; }

# ── 1. Check for cmake ───────────────────────────────────────
if ! command -v cmake &>/dev/null; then
    err "CMake is not installed. Please install CMake 3.22+ and re-run this script."
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
    fi
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
    case "$(uname -s)" in
        Linux)  install_qt6_linux ;;
        Darwin) install_qt6_macos ;;
        *)      err "Unsupported OS. Please install Qt6 manually." ;;
    esac
    # Re-check
    if ! check_qt6; then
        err "Qt6 still not found after installation attempt. Please install Qt6 (Core, Widgets, Network) manually."
    fi
fi

info "Qt6 found ✓"

# ── 3. Configure ─────────────────────────────────────────────
info "Configuring ($BUILD_TYPE) …"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    ${CMAKE_PREFIX_PATH:+-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"}

# ── 4. Build ─────────────────────────────────────────────────
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
info "Building with $NPROC parallel jobs …"
cmake --build "$BUILD_DIR" --parallel "$NPROC"

# ── 5. Run tests (if test binary was built) ──────────────────
if [ -f "$BUILD_DIR/SSA_Tests" ]; then
    info "Running tests …"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
else
    warn "Test binary not found — skipping tests."
fi

info "Build complete!  Binary: $BUILD_DIR/SSA"
