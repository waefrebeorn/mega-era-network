#!/bin/bash
# D34: Data freshness dashboard generator
# Checks last update time for all data sources and writes JSON for dashboard
# Cron: */15 * * * *

set -eo pipefail

TIMELINE_DB="$HOME/.hermes/pm_logs/timeline.db"
OUTPUT="/home/wubu2/money-room/docs/data/data_freshness.json"

mkdir -p "$(dirname "$OUTPUT")"

now=$(date +%s)

# Build JSON manually
{
echo "{\"generated\": $now, \"sources\": {"

# Source:pattern:threshold
SOURCES=(
    "yahoo:yahoo_:3600"
    "coingecko:coingecko_:1800"
    "news:news_:7200"
    "cboe:cboe_:900"
    "fear_greed:fear_greed:43200"
    "forex:forex_:3600"
    "fred:fred_:86400"
    "orderbook:orderbook_:300"
    "cvd:cvd_:600"
    "funding:funding_:1800"
)

first=1
for entry in "${SOURCES[@]}"; do
    IFS=':' read -r name pattern threshold <<< "$entry"
    latest=$(sqlite3 "$TIMELINE_DB" "SELECT COALESCE(MAX(timestamp),0) FROM timeline WHERE ticker LIKE '${pattern}%';" 2>/dev/null || echo "0")
    age=$((now - latest))
    if [ "$age" -gt "$threshold" ]; then status="STALE"
    elif [ "$age" -gt $((threshold / 2)) ]; then status="WARN"
    else status="OK"; fi
    
    if [ "$first" -eq 1 ]; then first=0; else echo ","; fi
    printf '  "%s": {"latest": %s, "age_sec": %s, "threshold": %s, "status": "%s"}' \
        "$name" "$latest" "$age" "$threshold" "$status"
done

echo ""
echo "}}"
} > "$OUTPUT"

echo "[D34] Data freshness written to $OUTPUT"
