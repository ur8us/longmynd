#!/bin/bash

# Usage: ./run_eardatek.sh [-f|--fast]
#   -f | --fast   Run VLC with 100ms network cache (lower latency)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

VLC_NET_CACHE=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -f|--fast)
            VLC_NET_CACHE=(--network-caching=100)
            shift
            ;;
        *)
            echo "Usage: $0 [-f|--fast]"
            exit 1
            ;;
    esac
done

# VLC password (must match -V argument to longmynd)
VLC_PASSWORD="longmynd"
VLC_HTTP_PORT=8082
LONGMYND_WEB_PORT=8080

# Stop existing instances and wait only if something is still exiting.
pkill -x longmynd 2>/dev/null
pkill -f "/snap/vlc" 2>/dev/null
pkill -x vlc 2>/dev/null

for _ in {1..20}; do
    if ! pgrep -x longmynd >/dev/null 2>&1 \
        && ! pgrep -x vlc >/dev/null 2>&1 \
        && ! pgrep -f "/snap/vlc" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

# Run longmynd with EARDA NIM and web interface
echo "Starting Longmynd web interface at http://localhost:${LONGMYND_WEB_PORT}/"
./longmynd -N earda -i 127.0.0.1 10000 -I 127.0.0.1 10001 -W $LONGMYND_WEB_PORT -V $VLC_PASSWORD -O $VLC_HTTP_PORT 1131500 1500 &
LONGMYND_PID=$!

# Run VLC with optional low-latency cache
VLC_BIN="$(command -v vlc || true)"
if [[ -n "$VLC_BIN" ]]; then
    "$VLC_BIN" "${VLC_NET_CACHE[@]}" --http-password=$VLC_PASSWORD --http-port=$VLC_HTTP_PORT udp://@:10000 &
    VLC_PID=$!
else
    echo "WARNING: vlc not found; Longmynd web and UDP TS output are still running"
    VLC_PID=""
fi

WEB_READY=0
if command -v curl >/dev/null 2>&1; then
    for _ in {1..20}; do
        if ! kill -0 "$LONGMYND_PID" 2>/dev/null; then
            echo "ERROR: longmynd exited before the web interface started"
            wait "$LONGMYND_PID"
            exit 1
        fi

        if curl -fsS --max-time 1 "http://127.0.0.1:${LONGMYND_WEB_PORT}/" >/dev/null 2>&1; then
            WEB_READY=1
            break
        fi
        sleep 0.1
    done

    if [[ "$WEB_READY" -eq 0 ]]; then
        echo "WARNING: Longmynd started but http://localhost:${LONGMYND_WEB_PORT}/ did not respond within 2 seconds"
    fi
fi

# Wait for all background processes
if [[ -n "$VLC_PID" ]]; then
    wait "$LONGMYND_PID" "$VLC_PID"
else
    wait "$LONGMYND_PID"
fi
