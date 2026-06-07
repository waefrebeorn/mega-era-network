#!/bin/bash
# start_secure_stack.sh - Start Money Room secure server stack
# Runs C data_server (port 9091) + nginx (port 8080/8443)

set -e

MONEY_ROOM_DIR="/home/wubu2/money-room"
NGINX_CONF="$MONEY_ROOM_DIR/nginx/nginx.conf"
DATA_SERVER="$MONEY_ROOM_DIR/engine/data_server"
PID_DIR="$MONEY_ROOM_DIR/nginx"

mkdir -p "$PID_DIR"

echo "=== Starting Money Room Secure Stack ==="

# 1. Start C data_server on port 9091 (if not running)
if ! pgrep -f "data_server.*9091" > /dev/null; then
    echo "Starting C data_server on port 9091..."
    cd "$MONEY_ROOM_DIR"
    nohup "$DATA_SERVER" 9091 > "$PID_DIR/data_server.log" 2>&1 &
    DATA_SERVER_PID=$!
    echo $DATA_SERVER_PID > "$PID_DIR/data_server.pid"
    sleep 2
    echo "data_server started (PID: $DATA_SERVER_PID)"
else
    echo "data_server already running"
fi

# 2. Start nginx on ports 8080 (HTTP->HTTPS redirect) and 8443 (HTTPS)
if ! pgrep -f "nginx.*$NGINX_CONF" > /dev/null; then
    echo "Starting nginx reverse proxy..."
    nginx -c "$NGINX_CONF"
    sleep 1
    echo "nginx started"
else
    echo "nginx already running"
fi

# 3. Verify endpoints
sleep 2
PASS="simplepass123"

echo ""
echo "=== Verification ==="
echo "Health check (no auth):"
curl -k -s https://localhost:8443/health

echo ""
echo "Main page (with auth):"
curl -k -u moneyroom:$PASS -s https://localhost:8443/index.html | head -5

echo ""
echo "API endpoint (with auth):"
curl -k -u moneyroom:$PASS -s https://localhost:8443/data/stats.json

echo ""
echo "=== Stack Ready ==="
echo "HTTPS:  https://localhost:8443  (user: moneyroom, pass: $PASS)"
echo "HTTP:   http://localhost:8080   (redirects to HTTPS)"
echo "Direct: http://localhost:9091   (C data_server, no auth)"
echo ""
echo "To stop: ./stop_secure_stack.sh"
