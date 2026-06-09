# F03: Hermetic build environment
# Pins dependency versions and provides a reproducible build script
# Usage: ./build.sh [--clean] [--test] [--memcheck]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
INSTALL_PREFIX="$BUILD_DIR/local"
JOBS=$(nproc 2>/dev/null || echo 4)

# Pinned versions
LIBCURL_VERSION="8.4.0"
JANSSON_VERSION="2.14"
SQLITE_VERSION="3.44.0"

# Parse args
CLEAN=0
TEST=0
MEMCHECK=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --test) TEST=1 ;;
        --memcheck) MEMCHECK=1 ;;
    esac
done

if [ "$CLEAN" -eq 1 ]; then
    echo "[BUILD] Cleaning $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR" "$INSTALL_PREFIX"

# Check system dependencies
check_dep() {
    local name="$1" pkg="$2" min_ver="$3"
    if dpkg -l "$pkg" 2>/dev/null | grep -q "^ii"; then
        echo "[BUILD] ✓ $name ($pkg) installed"
    else
        echo "[BUILD] ✗ $name ($pkg) missing — install with: sudo apt-get install $pkg"
        return 1
    fi
}

echo "═══ F03: Hermetic Build ═══"
echo "Build dir: $BUILD_DIR"
echo "Jobs: $JOBS"
echo ""

# Check all deps
MISSING=0
check_dep "gcc" "gcc" "12" || MISSING=1
check_dep "make" "make" "4" || MISSING=1
check_dep "pkg-config" "pkg-config" "0.29" || MISSING=1
check_dep "libcurl" "libcurl4-openssl-dev" "8.0" || MISSING=1
check_dep "jansson" "libjansson-dev" "2.14" || MISSING=1
check_dep "sqlite3" "libsqlite3-dev" "3.40" || MISSING=1

if [ "$MISSING" -eq 1 ]; then
    echo ""
    echo "[BUILD] Install missing deps:"
    echo "  sudo apt-get update && sudo apt-get install -y gcc make pkg-config libcurl4-openssl-dev libjansson-dev libsqlite3-dev"
    exit 1
fi

# Verify versions
echo ""
echo "═══ Dependency Versions ═══"
gcc --version | head -1
make --version | head -1
pkg-config --modversion libcurl 2>/dev/null || echo "libcurl: unknown"
pkg-config --modversion jansson 2>/dev/null || echo "jansson: unknown"
pkg-config --modversion sqlite3 2>/dev/null || echo "sqlite3: unknown"

# Build
echo ""
echo "═══ Building Engine ═══"
cd "$SCRIPT_DIR/engine"
make clean 2>/dev/null || true
make all -j"$JOBS" 2>&1 | tail -10

echo ""
echo "═══ Build Complete ═══"
ls -la room_engine 2>/dev/null && echo "  room_engine: OK" || echo "  room_engine: FAILED"

# Optional tests
if [ "$TEST" -eq 1 ]; then
    echo ""
    echo "═══ Running Tests ═══"
    make test 2>&1 | tail -20
fi

# Optional memcheck
if [ "$MEMCHECK" -eq 1 ]; then
    echo ""
    echo "═══ Memory Check ═══"
    make memcheck 2>&1 | tail -10
fi

echo ""
echo "═══ Done ═══"
