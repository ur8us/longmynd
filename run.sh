#!/bin/bash

# VLC password (must match -V argument to longmynd)
VLC_PASSWORD="longmynd"
VLC_HTTP_PORT=8082
LONGMYND_WEB_PORT=8080

# Run longmynd with EARDA NIM and web interface
./longmynd -N earda -i 127.0.0.1 10000 -I 127.0.0.1 10001 -W $LONGMYND_WEB_PORT -V $VLC_PASSWORD -O $VLC_HTTP_PORT 1131500 1500 &

# Run VLC to consume the TS stream (HTTP interface on separate port)
vlc --http-password=$VLC_PASSWORD --http-port=$VLC_HTTP_PORT udp://@:10000 &

# Wait for all background processes
wait
