#!/bin/sh
# Run the hall-of-fame API next to nginx in the same container.
mkdir -p /data
python3 /app/leaderboard.py &
exec nginx -g 'daemon off;'
