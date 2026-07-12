#!/bin/sh
# Run the hall-of-fame API and the SSH game server next to nginx in the
# same container. /data holds the scores DB and the SSH host key.
mkdir -p /data
python3 /app/leaderboard.py &
/app/ktd-ssh &
exec nginx -g 'daemon off;'
