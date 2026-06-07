#!/bin/bash
# stop_secure_stack.sh - Stop Money Room secure server stack

set -e

MONEY_ROOM_DIR="/home/wubu2/money-room"
NGINX_CONF="$MONEY_ROOM_DIR/nginx/nginx.conf"
PID_DIR="$MONEY_ROOM_DIR/nginx"

echo "=== Stopping Money Room Secure Stack ==="

# Stop nginx
if pgrep -f "nginx.*$NGINX_CONF" > /dev/null; then
    echo "Stopping nginx..."
    nginx -c "$NGINX_CONF" -s quit 2>/dev/null || pkill -f "nginx.*$NGINX_CONF"
    echo "nginx stopped"
else
    echo "nginx not running"
fi

# Stop C data_server
if pgrep -f "data_server.*9091" > /dev/null; then
    echo "Stopping data_server..."
    pkill -f "data_server.*9091"
    echo "data_server stopped"
else
    echo "data_server not running"
fi

# Clean up PID files
rm -f "$PID_DIR/data_server.pid" "$PID_DIR/nginx.pid"

echo "=== Stack Stopped ==="
