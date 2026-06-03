#!/bin/bash
# F18: Performance benchmark suite for money-room engine
set -e
cd "$(dirname "$0")/../engine"

echo "═══ F18: Performance Benchmark Suite ═══"
echo ""

# 1. Build time benchmark
echo "── Build Time ──"
BUILD_START=$(date +%s%N)
make -j$(nproc) room_engine > /dev/null 2>&1
BUILD_END=$(date +%s%N)
BUILD_MS=$(( (BUILD_END - BUILD_START) / 1000000 ))
echo "room_engine build: ${BUILD_MS}ms"
[ "$BUILD_MS" -gt 10000 ] && echo "WARN: Build >10s"

# 2. Binary size benchmark
echo ""
echo "── Binary Size ──"
ENGINE_SIZE=$(stat -f%z room_engine 2>/dev/null || stat -c%s room_engine 2>/dev/null)
echo "room_engine: $(( ENGINE_SIZE / 1024 ))KB"
[ "$ENGINE_SIZE" -gt 1048576 ] && echo "WARN: Binary >1MB"

# 3. Memory benchmark (if state file exists)
echo ""
echo "── Memory Usage ──"
STATE_FILE="../data/room_state.bin"
if [ -f "$STATE_FILE" ]; then
    STATE_SIZE=$(stat -f%z "$STATE_FILE" 2>/dev/null || stat -c%s "$STATE_FILE" 2>/dev/null)
    echo "room_state.bin: $(( STATE_SIZE / 1024 ))KB"
    echo "RoomState struct: ~$(( STATE_SIZE / 1024 ))KB (must be < 50MB)"
else
    echo "room_state.bin: not found (skip)"
fi

# 4. Tool build benchmarks
echo ""
echo "── Tool Build Times ──"
for tool in accuracy_scorer data_quality stress_test health_check; do
    if [ -f "${tool}.c" ]; then
        T0=$(date +%s%N)
        make "$tool" > /dev/null 2>&1 || true
        T1=$(date +%s%N)
        T_MS=$(( (T1 - T0) / 1000000 ))
        echo "  $tool: ${T_MS}ms"
    fi
done

# 5. Memcheck benchmark (if valgrind available)
echo ""
echo "── Memory Leaks ──"
if command -v valgrind > /dev/null 2>&1; then
    if [ -f accuracy_scorer ]; then
        LEAKS=$(valgrind --leak-check=summary ./accuracy_scorer 2>&1 | grep "definitely lost" || echo "none")
        echo "accuracy_scorer: $LEAKS"
    fi
else
    echo "valgrind not installed (skip)"
fi

echo ""
echo "═══ Benchmark Complete ═══"
