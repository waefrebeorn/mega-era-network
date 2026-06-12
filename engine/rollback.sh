#!/bin/bash
# rollback.sh — Emergency rollback for room_engine
# Usage: ./rollback.sh [list|restore|backup]
#
# Maintains last 3 room_engine binaries. If new build breaks,
# restores previous known-good binary.

ENGINE_DIR="$(dirname "$0")"
BACKUP_DIR="$ENGINE_DIR/.rollback"
CURRENT="$ENGINE_DIR/room_engine"
PAPER="$ENGINE_DIR/room_engine_paper"
MARKET="$ENGINE_DIR/room_engine_market"

mkdir -p "$BACKUP_DIR"

case "${1:-status}" in
    backup)
        # Save current binaries before upgrade
        TS=$(date +%Y%m%d_%H%M%S)
        if [ -x "$CURRENT" ]; then
            cp "$CURRENT" "$BACKUP_DIR/room_engine.$TS"
            cp "$CURRENT" "$BACKUP_DIR/room_engine.latest"
            echo "[ROLLBACK] Backed up room_engine ($TS)"
        fi
        if [ -x "$PAPER" ]; then
            cp "$PAPER" "$BACKUP_DIR/room_engine_paper.$TS"
            cp "$PAPER" "$BACKUP_DIR/room_engine_paper.latest"
            echo "[ROLLBACK] Backed up room_engine_paper ($TS)"
        fi
        if [ -x "$MARKET" ]; then
            cp "$MARKET" "$BACKUP_DIR/room_engine_market.$TS"
            cp "$MARKET" "$BACKUP_DIR/room_engine_market.latest"
            echo "[ROLLBACK] Backed up room_engine_market ($TS)"
        fi
        # Prune: keep only last 3 backups per binary
        for prefix in room_engine room_engine_paper room_engine_market; do
            ls -t "$BACKUP_DIR/$prefix".* 2>/dev/null | tail -n +4 | xargs rm -f
        done
        echo "[ROLLBACK] Pruned old backups (keep 3)"
        ;;
    restore)
        # Restore previous binary
        PREV=$(ls -t "$BACKUP_DIR"/room_engine.2* 2>/dev/null | head -1)
        if [ -z "$PREV" ]; then
            PREV="$BACKUP_DIR/room_engine.latest"
        fi
        if [ -f "$PREV" ]; then
            cp "$PREV" "$CURRENT"
            chmod +x "$CURRENT"
            echo "[ROLLBACK] Restored room_engine from $PREV"
        else
            echo "[ROLLBACK] No backup found in $BACKUP_DIR"
            exit 1
        fi
        # Also restore paper/market if available
        PREV_PAPER=$(ls -t "$BACKUP_DIR"/room_engine_paper.2* 2>/dev/null | head -1)
        if [ -z "$PREV_PAPER" ]; then PREV_PAPER="$BACKUP_DIR/room_engine_paper.latest"; fi
        if [ -f "$PREV_PAPER" ] && [ -f "$PAPER" ]; then
            cp "$PREV_PAPER" "$PAPER"
            chmod +x "$PAPER"
            echo "[ROLLBACK] Restored room_engine_paper"
        fi
        ;;
    list)
        echo "=== Rollback backups ==="
        ls -la "$BACKUP_DIR"/*.latest "$BACKUP_DIR"/*.2* 2>/dev/null || echo "(no backups)"
        echo ""
        echo "=== Current binaries ==="
        ls -la "$CURRENT" "$PAPER" "$MARKET" 2>/dev/null
        ;;
    status)
        echo "=== Rollback status ==="
        LATEST="$BACKUP_DIR/room_engine.latest"
        if [ -f "$LATEST" ]; then
            echo "Latest backup: $(stat -c '%y' "$LATEST")"
            echo "Current:       $(stat -c '%y' "$CURRENT" 2>/dev/null || echo 'missing')"
            if [ -x "$CURRENT" ] && [ -f "$LATEST" ]; then
                SAME=$(cmp -s "$CURRENT" "$LATEST" && echo "YES" || echo "NO")
                echo "Current == Latest: $SAME"
            fi
        else
            echo "No backup yet. Run: ./rollback.sh backup"
        fi
        ;;
    *)
        echo "Usage: $0 {backup|restore|list|status}"
        exit 1
        ;;
esac
