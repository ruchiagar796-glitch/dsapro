#!/bin/sh
# A persistent disk (for example Render's) is mounted owned by root, but the server must NOT run
# as root. So: fix the ownership of the data folder, then drop privileges and start the server.
set -e
DATA="${BMS_DATA_DIR:-/data}"
mkdir -p "$DATA"
if [ "$(id -u)" = "0" ]; then
  chown -R bank "$DATA" 2>/dev/null || echo "warning: could not change ownership of $DATA" >&2
  exec setpriv --reuid=bank --regid=bank --clear-groups ./bank_server "$@"
fi
exec ./bank_server "$@"
