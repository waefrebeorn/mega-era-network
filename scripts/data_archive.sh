#!/bin/bash
# D53: Data compression archive
# Compresses data files older than 30 days into monthly tar.gz archives
# Run monthly via cron

set -eo pipefail

DATA_DIR="/home/wubu2/money-room/data"
ARCHIVE_DIR="$DATA_DIR/archives"
RETENTION_DAYS=365  # Keep archives for 1 year

mkdir -p "$ARCHIVE_DIR"

# Compress CSV files older than 30 days
find "$DATA_DIR" -maxdepth 2 -name "*.csv" -mtime +30 -type f | while read -r f; do
    month=$(date -r "$f" +%Y-%m)
    archive="$ARCHIVE_DIR/${month}.tar.gz"
    
    if [ -f "$archive" ]; then
        # Append to existing archive
        tar -rf "${archive%.gz}" "$f" 2>/dev/null && gzip -f "${archive%.gz}" 2>/dev/null || true
    else
        # Create new archive
        tar -czf "$archive" "$f" 2>/dev/null || true
    fi
    
    # Remove original if archived
    if [ -f "$archive" ]; then
        rm -f "$f"
        echo "[ARCHIVE] Compressed $f → $archive"
    fi
done

# Prune archives older than retention period
find "$ARCHIVE_DIR" -name "*.tar.gz" -mtime +$RETENTION_DAYS -delete 2>/dev/null || true

echo "[D53] Data compression archive complete"
