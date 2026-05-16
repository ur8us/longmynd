#!/bin/bash

# Run longmynd with EARDA NIM and web interface
./longmynd -N earda -i 127.0.0.1 10000 -I 127.0.0.1 10001 -W 8080 1131500 1500 &

# Run VLC to consume the TS stream
vlc udp://@:10000 &

# Wait for all background processes
wait
