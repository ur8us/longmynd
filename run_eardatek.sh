#!/bin/bash

# Usage: ./run_eardatek.sh [-f|--fast]
#   -f | --fast   Run VLC with 100ms network cache (lower latency)
VLC_NET_CACHE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -f|--fast)
            VLC_NET_CACHE="--network-caching=100"
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

# Kill existing instances
pkill -f "./longmynd" 2>/dev/null
pkill -f "/snap/vlc" 2>/dev/null
pkill vlc 2>/dev/null
sleep 1

# Run longmynd with EARDA NIM and web interface
./longmynd -N earda -i 127.0.0.1 10000 -I 127.0.0.1 10001 -W $LONGMYND_WEB_PORT -V $VLC_PASSWORD -O $VLC_HTTP_PORT 1131500 1500 &

# Run VLC with optional low-latency cache
vlc $VLC_NET_CACHE --http-password=$VLC_PASSWORD --http-port=$VLC_HTTP_PORT udp://@:10000 &

# Wait for all background processes
wait
