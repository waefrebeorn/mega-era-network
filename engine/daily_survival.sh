#!/bin/bash
# daily_survival.sh — $50/day paper-to-IRL survival engine
# 
# Flow:
# 1. Check if paper engine has trained agents (50+ trades, 52%+ WR)
# 2. If yes, compute live signal from top survivor
# 3. Write pending trade to JSON for human confirmation
# 4. If wallet funded, await confirmation then execute
#
# Cron: daily at 8am (before market open)
# Usage: ./daily_survival.sh [--force] [--dry-run]

set -euo pipefail

ENGINE_DIR="/home/wubu2/money-room/engine"
DATA_DIR="/home/wubu2/money-room/data"
LIVE_STATE="$DATA_DIR/live_state.json"
PENDING_TRADE="$DATA_DIR/pending_trade.json"
WALLET_STATE="/home/wubu2/.hermes/infra/live_clob_state.json"

FORCE=0
DRY_RUN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) FORCE=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        *) shift ;;
    esac
done

echo "[SURVIVAL] $(date) — Daily survival check"

# ── Step 1: Check wallet funding ──
WALLET_FUNDED=0
if [[ -f "$WALLET_STATE" ]]; then
    USDC=$(python3 -c "import json; d=json.load(open('$WALLET_STATE')); print(d.get('usdc_balance', 0))" 2>/dev/null || echo "0")
    MATIC=$(python3 -c "import json; d=json.load(open('$WALLET_STATE')); print(d.get('matic_balance', 0))" 2>/dev/null || echo "0")
    echo "[SURVIVAL] Wallet: USDC=$USDC MATIC=$MATIC"
    if (( $(echo "$USDC >= 50" | bc -l 2>/dev/null || echo "0") )); then
        WALLET_FUNDED=1
    fi
else
    echo "[SURVIVAL] No wallet state file — checking polygon_monitor"
    USDC="0"
    MATIC="0"
fi

# ── Step 2: Load survivor queue from paper engine ──
echo "[SURVIVAL] Loading survivor queue..."
cd "$ENGINE_DIR"

# Check if paper state exists
PAPER_STATE="/home/wubu2/.hermes/pm_logs/c_room/room_state_paper.bin"
if [[ ! -f "$PAPER_STATE" ]]; then
    echo "[SURVIVAL] No paper state file — paper engine hasn't run yet"
    echo "[SURVIVAL] ACTION: Start paper engine first: cd $ENGINE_DIR && ./room_engine_paper &"
    exit 0
fi

# Get survivor status
SURVIVOR_OUTPUT=$(./live_trader --status 2>&1)
echo "$SURVIVOR_OUTPUT"

# Check if any survivors qualified
N_SURVIVORS=$(echo "$SURVIVOR_OUTPUT" | grep -cE "^\s+[0-9]+" 2>/dev/null || true)
N_SURVIVORS=${N_SURVIVORS:-0}
if [ "$N_SURVIVORS" -lt 1 ]; then
    echo "[SURVIVAL] No survivors qualified yet"
    echo "[SURVIVAL] Requirements: 50+ paper trades, 52%+ WR, \$10+ PnL"
    echo "[SURVIVAL] Paper engine needs more training cycles"
    exit 0
fi

# Extract top survivor
TOP_AGENT=$(echo "$SURVIVOR_OUTPUT" | grep "^\s*[0-9]" | head -1 | awk '{print $1}')
TOP_PNL=$(echo "$SURVIVOR_OUTPUT" | grep "^\s*[0-9]" | head -1 | awk '{print $2}')
TOP_WR=$(echo "$SURVIVOR_OUTPUT" | grep "^\s*[0-9]" | head -1 | awk '{print $3}')
TOP_TRADES=$(echo "$SURVIVOR_OUTPUT" | grep "^\s*[0-9]" | head -1 | awk '{print $4}')

echo "[SURVIVAL] Top survivor: agent #$TOP_AGENT PnL=$TOP_PNL WR=$TOP_WR trades=$TOP_TRADES"

# ── Step 3: Compute live signal ──
echo "[SURVIVAL] Computing live signal..."
if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "[SURVIVAL] DRY RUN — would run: ./live_trader --mode candidate"
    # Generate a sample pending trade for testing
    cat > "$PENDING_TRADE" << EOF
{
  "action": "BUY_YES",
  "stake": 10.00,
  "confidence": 0.62,
  "wallet_usd": 50.00,
  "reason": "DRY RUN: delta=+0.0012 mom=+0.0008 acc=+0.0003 rsi=58.3 conf=62%",
  "agent_id": $TOP_AGENT,
  "paper_pnl": $TOP_PNL,
  "paper_wr": $TOP_WR,
  "paper_trades": $TOP_TRADES,
  "timestamp": $(date +%s),
  "status": "PENDING_CONFIRMATION",
  "dry_run": true
}
EOF
else
    # Run live trader in candidate mode (computes signal, writes pending_trade.json)
    timeout 30 ./live_trader --mode candidate 2>&1 || true
fi

# ── Step 4: Check if pending trade was generated ──
if [[ ! -f "$PENDING_TRADE" ]]; then
    echo "[SURVIVAL] No pending trade generated — signal too weak or no edge"
    exit 0
fi

# Parse pending trade
TRADE_ACTION=$(python3 -c "import json; d=json.load(open('$PENDING_TRADE')); print(d.get('action', 'NONE'))" 2>/dev/null || echo "NONE")
TRADE_STAKE=$(python3 -c "import json; d=json.load(open('$PENDING_TRADE')); print(d.get('stake', 0))" 2>/dev/null || echo "0")
TRADE_CONF=$(python3 -c "import json; d=json.load(open('$PENDING_TRADE')); print(d.get('confidence', 0))" 2>/dev/null || echo "0")
TRADE_REASON=$(python3 -c "import json; d=json.load(open('$PENDING_TRADE')); print(d.get('reason', ''))" 2>/dev/null || echo "")

WEAK_SIGNAL=$(python3 -c "
conf = float('$TRADE_CONF')
print(1 if conf < 0.55 else 0)
" 2>/dev/null || echo "1")

if [[ "$TRADE_ACTION" == "NONE" ]] || [[ "$WEAK_SIGNAL" -eq 1 ]]; then
    echo "[SURVIVAL] Signal too weak (conf=$TRADE_CONF) — skipping"
    rm -f "$PENDING_TRADE"
    exit 0
fi

echo "[SURVIVAL] Pending trade: $TRADE_ACTION \$$TRADE_STAKE (conf=$TRADE_CONF)"
echo "[SURVIVAL] Reason: $TRADE_REASON"

# ── Step 5: Check daily limits ──
TODAY=$(date +%Y-%m-%d)
DAILY_PNL=$(python3 -c "
import json, os
f = '$LIVE_STATE'
if os.path.exists(f):
    d = json.load(open(f))
    if d.get('date') == '$TODAY':
        print(d.get('daily_pnl', 0))
    else:
        print(0)
else:
    print(0)
" 2>/dev/null || echo "0")

DAILY_TRADES=$(python3 -c "
import json, os
f = '$LIVE_STATE'
if os.path.exists(f):
    d = json.load(open(f))
    if d.get('date') == '$TODAY':
        print(d.get('daily_trades', 0))
    else:
        print(0)
else:
    print(0)
" 2>/dev/null || echo "0")

echo "[SURVIVAL] Today: PnL=\$$DAILY_PNL Trades=$DAILY_TRADES"

# Check daily loss limit (using python for float math)
DAILY_HALT=$(python3 -c "
pnl = float('$DAILY_PNL')
print(1 if pnl < -5.0 else 0)
" 2>/dev/null || echo "0")

if [[ "$DAILY_HALT" -eq 1 ]]; then
    echo "[SURVIVAL] Daily loss limit hit (\$$DAILY_PNL) — HALTING"
    echo "[SURVIVAL] Trading halted for today. Will retry tomorrow."
    exit 0
fi

# Check max trades per day (3 max for $50/day budget)
if [[ "$DAILY_TRADES" -ge 3 ]]; then
    echo "[SURVIVAL] Max daily trades reached ($DAILY_TRADES) — skipping"
    exit 0
fi

# ── Step 6: Execute or await confirmation ──
if [[ "$WALLET_FUNDED" -eq 1 ]] && [[ "$DRY_RUN" -eq 0 ]]; then
    echo "[SURVIVAL] Wallet funded — awaiting human confirmation"
    echo "[SURVIVAL] Trade written to $PENDING_TRADE"
    echo "[SURVIVAL] Send /confirm to execute or /skip to cancel"
    # The Telegram bot will pick up pending_trade.json and ask for confirmation
    # After confirmation, it will call pm_live_clob.py with the trade details
else
    if [[ "$DRY_RUN" -eq 1 ]]; then
        echo "[SURVIVAL] DRY RUN — trade would be: $TRADE_ACTION \$$TRADE_STAKE"
    else
        echo "[SURVIVAL] Wallet not funded — trade queued for when wallet is ready"
        echo "[SURVIVAL] Fund wallet with \$50 USDC + \$2 MATIC on Polygon"
    fi
fi

# ── Step 7: Update live state ──
python3 -c "
import json, os
f = '$LIVE_STATE'
state = {}
if os.path.exists(f):
    state = json.load(open(f))
state['date'] = '$TODAY'
state['last_check'] = '$(date -Iseconds)'
state['top_agent'] = $TOP_AGENT
state['top_pnl'] = float('$TOP_PNL')
state['top_wr'] = float('$TOP_WR')
state['top_trades'] = int('$TOP_TRADES')
state['wallet_funded'] = $WALLET_FUNDED
state['pending_trade'] = '$TRADE_ACTION'
state['pending_stake'] = float('$TRADE_STAKE')
state['pending_conf'] = float('$TRADE_CONF')
state['daily_pnl'] = float('$DAILY_PNL')
state['daily_trades'] = int('$DAILY_TRADES')
with open(f, 'w') as fp:
    json.dump(state, fp, indent=2)
" 2>/dev/null || true

echo "[SURVIVAL] Complete. State saved to $LIVE_STATE"
