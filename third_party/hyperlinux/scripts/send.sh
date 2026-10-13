#!/usr/bin/env bash
# Send a payload to the PS5 payload loader (elfldr, default port 9021).
#
# Usage:
#   ./scripts/send.sh [payload.elf]
#
# Environment overrides:
#   PS5_IP    (default 192.168.1.159)
#   PS5_PORT  (default 9021)
set -euo pipefail

PS5_IP="${PS5_IP:-192.168.1.159}"
PS5_PORT="${PS5_PORT:-9021}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAYLOAD="${1:-$REPO_ROOT/bin/pLinux5-NG.elf}"

if [[ ! -f "$PAYLOAD" ]]; then
  echo "payload not found: $PAYLOAD" >&2
  exit 1
fi

echo "sending $(basename "$PAYLOAD") -> $PS5_IP:$PS5_PORT"

if command -v nc >/dev/null 2>&1; then
  # -N shuts the socket down after EOF so the loader knows the payload ended.
  nc -N "$PS5_IP" "$PS5_PORT" < "$PAYLOAD"
elif command -v socat >/dev/null 2>&1; then
  socat -t 99999999 - "TCP:$PS5_IP:$PS5_PORT" < "$PAYLOAD"
else
  echo "need either 'nc' or 'socat' installed" >&2
  exit 1
fi

echo "done"
