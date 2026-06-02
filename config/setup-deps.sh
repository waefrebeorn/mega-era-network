#!/bin/bash
# setup-deps.sh — F35: Automated dependency installation for Money Room
# Installs libcurl, jansson, sqlite3, and other build dependencies
# Usage: bash setup-deps.sh

set -e

echo "=== Money Room Dependency Installer ==="

# Detect package manager
if command -v apt-get &> /dev/null; then
    PKG="apt-get"
    INSTALL="sudo apt-get install -y"
elif command -v yum &> /dev/null; then
    PKG="yum"
    INSTALL="sudo yum install -y"
elif command -v pacman &> /dev/null; then
    PKG="pacman"
    INSTALL="sudo pacman -S --noconfirm"
elif command -v apk &> /dev/null; then
    PKG="apk"
    INSTALL="sudo apk add"
else
    echo "ERROR: No supported package manager found (apt/yum/pacman/apk)"
    exit 1
fi

echo "Detected package manager: $PKG"

# Core build tools
echo "Installing build tools..."
$INSTALL gcc make pkg-config

# Money Room dependencies
echo "Installing libcurl..."
$INSTALL libcurl4-openssl-dev 2>/dev/null || \
    $INSTALL libcurl-devel 2>/dev/null || \
    $INSTALL curl 2>/dev/null || \
    echo "WARNING: libcurl dev package not found, trying to continue..."

echo "Installing jansson..."
$INSTALL libjansson-dev 2>/dev/null || \
    $INSTALL jansson-devel 2>/dev/null || \
    $INSTALL jansson 2>/dev/null || \
    echo "WARNING: jansson dev package not found, trying to continue..."

echo "Installing sqlite3..."
$INSTALL libsqlite3-dev 2>/dev/null || \
    $INSTALL sqlite3-devel 2>/dev/null || \
    $INSTALL sqlite3 2>/dev/null || \
    echo "WARNING: sqlite3 dev package not found, trying to continue..."

# Optional: valgrind for memory checking
echo "Installing valgrind (optional)..."
$INSTALL valgrind 2>/dev/null || \
    echo "NOTE: valgrind not available (optional)"

# Verify installations
echo ""
echo "=== Verification ==="
echo -n "gcc: " && gcc --version | head -1
echo -n "make: " && make --version | head -1

# Check for headers
echo -n "libcurl: "
if [ -f /usr/include/curl/curl.h ] || [ -f /usr/local/include/curl/curl.h ]; then
    echo "OK"
else
    echo "NOT FOUND (install libcurl-dev)"
fi

echo -n "jansson: "
if [ -f /usr/include/jansson.h ] || [ -f /usr/local/include/jansson.h ]; then
    echo "OK"
else
    echo "NOT FOUND (install libjansson-dev)"
fi

echo -n "sqlite3: "
if [ -f /usr/include/sqlite3.h ] || [ -f /usr/local/include/sqlite3.h ]; then
    echo "OK"
else
    echo "NOT FOUND (install libsqlite3-dev)"
fi

echo ""
echo "=== Setup complete ==="
echo "Build money-room: cd money-room/engine && make all"
