#!/bin/bash
#
# Stop the OpenACR WASM demo server and ngrok
# Usage: ./stop.sh [--server-only]
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="$SCRIPT_DIR/.server.pid"
NGROK_PID_FILE="$SCRIPT_DIR/.ngrok.pid"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SERVER_ONLY=false
for arg in "$@"; do
    [ "$arg" = "--server-only" ] && SERVER_ONLY=true
done

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}  Stopping WASM Demo Server${NC}"
echo -e "${YELLOW}========================================${NC}"
echo ""

STOPPED_SOMETHING=false

# Stop server
echo "Checking WASM server..."
if [ -f "$PID_FILE" ]; then
    SERVER_PID=$(cat "$PID_FILE")
    if ps -p "$SERVER_PID" > /dev/null 2>&1; then
        echo "  Stopping server (PID: $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null
        for i in {1..10}; do
            if ! ps -p "$SERVER_PID" > /dev/null 2>&1; then break; fi
            sleep 0.5
        done
        if ps -p "$SERVER_PID" > /dev/null 2>&1; then
            kill -9 "$SERVER_PID" 2>/dev/null
        fi
        echo -e "  ${GREEN}Server stopped${NC}"
        STOPPED_SOMETHING=true
    else
        echo "  Server not running (stale PID file)"
    fi
    rm -f "$PID_FILE"
else
    SERVER_PIDS=$(lsof -i :9090 -t 2>/dev/null)
    if [ -n "$SERVER_PIDS" ]; then
        for PID in $SERVER_PIDS; do
            echo "  Stopping server process $PID..."
            kill "$PID" 2>/dev/null
            STOPPED_SOMETHING=true
        done
        sleep 1
        echo -e "  ${GREEN}Server stopped${NC}"
    else
        echo "  No server running"
    fi
fi

# Stop ngrok
echo ""
if [ "$SERVER_ONLY" = true ]; then
    echo -e "${CYAN}Keeping ngrok running (--server-only mode)${NC}"
else
    echo "Checking ngrok..."
    if [ -f "$NGROK_PID_FILE" ]; then
        NGROK_PID=$(cat "$NGROK_PID_FILE")
        if ps -p "$NGROK_PID" > /dev/null 2>&1; then
            echo "  Stopping ngrok (PID: $NGROK_PID)..."
            kill "$NGROK_PID" 2>/dev/null
            sleep 1
            echo -e "  ${GREEN}Ngrok stopped${NC}"
            STOPPED_SOMETHING=true
        fi
        rm -f "$NGROK_PID_FILE"
    fi

    NGROK_PIDS=$(pgrep -f "ngrok" 2>/dev/null)
    if [ -n "$NGROK_PIDS" ]; then
        for PID in $NGROK_PIDS; do
            echo "  Stopping ngrok process $PID..."
            kill "$PID" 2>/dev/null
            STOPPED_SOMETHING=true
        done
        sleep 1
        echo -e "  ${GREEN}Ngrok stopped${NC}"
    else
        [ "$STOPPED_SOMETHING" = false ] && echo "  No ngrok running"
    fi
fi

echo ""
if [ "$STOPPED_SOMETHING" = true ]; then
    echo -e "${GREEN}All services stopped${NC}"
else
    echo -e "${YELLOW}Nothing was running${NC}"
fi
echo ""
echo -e "To start again: ${GREEN}./start.sh${NC}"
