#!/bin/bash
#
# Start the OpenACR WASM demo server with optional ngrok tunnel
# Usage: ./start.sh [--no-ngrok]
#

PORT=9090
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="$SCRIPT_DIR/.server.pid"
NGROK_PID_FILE="$SCRIPT_DIR/.ngrok.pid"
LOG_FILE="$SCRIPT_DIR/server.log"
NGROK_LOG_FILE="$SCRIPT_DIR/ngrok.log"

NO_NGROK=false
for arg in "$@"; do
    [ "$arg" = "--no-ngrok" ] && NO_NGROK=true
done

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  OpenACR WASM Demo Server${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Check if server already running
if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE")
    if ps -p "$OLD_PID" > /dev/null 2>&1; then
        echo -e "${YELLOW}Server already running (PID: $OLD_PID)${NC}"

        # Show tunnels if ngrok running
        TUNNELS=$(curl -s http://localhost:4040/api/tunnels 2>/dev/null)
        if [ -n "$TUNNELS" ] && [ "$TUNNELS" != "null" ]; then
            echo ""
            echo -e "${CYAN}Active tunnels:${NC}"
            echo "$TUNNELS" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    for t in data.get('tunnels', []):
        print(f\"  {t['name']}: {t['public_url']}\")
except: pass
" 2>/dev/null
        fi
        echo ""
        echo -e "To stop: ${YELLOW}./stop.sh${NC}"
        exit 0
    else
        rm -f "$PID_FILE"
    fi
fi

# Check node
if ! command -v node &> /dev/null; then
    echo -e "${RED}Error: node not found${NC}"
    exit 1
fi

# Install deps if needed
if [ ! -d "$SCRIPT_DIR/node_modules" ]; then
    echo "Installing dependencies..."
    cd "$SCRIPT_DIR" && npm install
fi

# Start server
echo "Starting WASM server on port $PORT..."
cd "$SCRIPT_DIR"
nohup node server.js > "$LOG_FILE" 2>&1 &
SERVER_PID=$!
echo $SERVER_PID > "$PID_FILE"

sleep 2
if ! ps -p $SERVER_PID > /dev/null 2>&1; then
    echo -e "${RED}Failed to start server${NC}"
    tail -10 "$LOG_FILE"
    rm -f "$PID_FILE"
    exit 1
fi
echo -e "${GREEN}Server started!${NC} (PID: $SERVER_PID)"

# Start ngrok
if [ "$NO_NGROK" = false ] && command -v ngrok &> /dev/null; then
    echo ""

    # Check if ngrok already running with tunnels
    EXISTING=$(curl -s http://localhost:4040/api/tunnels 2>/dev/null | grep -o "public_url" | wc -l | tr -d ' ')
    EXISTING=${EXISTING:-0}

    if [ "$EXISTING" -gt 0 ] 2>/dev/null; then
        echo -e "${CYAN}Ngrok already running with tunnels${NC}"
    else
        echo "Starting ngrok tunnel (port $PORT)..."

        pkill -f "ngrok" 2>/dev/null
        sleep 1

        nohup ngrok http $PORT --log=stdout > "$NGROK_LOG_FILE" 2>&1 &
        NGROK_PID=$!
        echo $NGROK_PID > "$NGROK_PID_FILE"

        echo -n "Establishing tunnel"
        for i in {1..15}; do
            sleep 1
            echo -n "."
            COUNT=$(curl -s http://localhost:4040/api/tunnels 2>/dev/null | grep -o "public_url" | wc -l | tr -d ' ')
            COUNT=${COUNT:-0}
            if [ "$COUNT" -ge 1 ] 2>/dev/null; then
                break
            fi
        done
        echo ""
    fi
fi

# Display info
TAILSCALE_IP=$(tailscale ip -4 2>/dev/null)
LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}')

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Server is running!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "  ${CYAN}Local:${NC}      http://localhost:$PORT"
if [ -n "$LOCAL_IP" ]; then
    echo -e "  ${CYAN}Network:${NC}    http://${LOCAL_IP}:$PORT"
fi
if [ -n "$TAILSCALE_IP" ]; then
    echo -e "  ${CYAN}Tailscale:${NC}  http://${TAILSCALE_IP}:$PORT"
fi

# Show ngrok tunnel
TUNNELS=$(curl -s http://localhost:4040/api/tunnels 2>/dev/null)
if [ -n "$TUNNELS" ] && [ "$TUNNELS" != "null" ]; then
    PUBLIC_URL=$(echo "$TUNNELS" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    for t in data.get('tunnels', []):
        print(t.get('public_url', ''))
        break
except: pass
" 2>/dev/null)
    if [ -n "$PUBLIC_URL" ]; then
        echo -e "  ${CYAN}Ngrok:${NC}      $PUBLIC_URL"
    fi
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "To stop: ${YELLOW}./stop.sh${NC}"
