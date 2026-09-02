#!/usr/bin/env bash
# deploy_run.sh — Deploy binary to board and run it.
# Parameterized: IP/user/password via args or env, NEVER hardcoded.
#
# Usage:
#   ./deploy_run.sh --ip 192.168.1.100 --user root --password 123456 \
#                   --local ./app --remote /tmp/app --args "--input test.h264"
#   # or via env:
#   BOARD_IP=192.168.1.100 BOARD_USER=root BOARD_PW=123456 \
#   ./deploy_run.sh --local ./app
set -euo pipefail

IP="${BOARD_IP:-}"
USER="${BOARD_USER:-root}"
PW="${BOARD_PW:-}"
LOCAL=""
REMOTE="/tmp/app"
ARGS=""
PORT=22

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ip) IP="$2"; shift 2 ;;
        --user) USER="$2"; shift 2 ;;
        --password) PW="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --local) LOCAL="$2"; shift 2 ;;
        --remote) REMOTE="$2"; shift 2 ;;
        --args) ARGS="$2"; shift 2 ;;
        *) echo "unknown: $1"; exit 1 ;;
    esac
done

[[ -n "$IP" ]]   || { echo "ERROR: --ip or BOARD_IP required"; exit 1; }
[[ -n "$PW" ]]   || { echo "ERROR: --password or BOARD_PW required"; exit 1; }
[[ -n "$LOCAL" ]] || { echo "ERROR: --local required"; exit 1; }
[[ -f "$LOCAL" ]] || { echo "ERROR: $LOCAL not found"; exit 1; }

REMOTE="${REMOTE:-/tmp/$(basename "$LOCAL")}"

echo "==> deploy $LOCAL -> $USER@$IP:$REMOTE"
sshpass -p "$PW" scp -o StrictHostKeyChecking=no -P "$PORT" "$LOCAL" "$USER@$IP:$REMOTE" \
    || { echo "scp failed"; exit 1; }
sshpass -p "$PW" ssh -o StrictHostKeyChecking=no -p "$PORT" "$USER@$IP" "chmod +x $REMOTE"

echo "==> run $REMOTE $ARGS"
sshpass -p "$PW" ssh -o StrictHostKeyChecking=no -p "$PORT" "$USER@$IP" "$REMOTE $ARGS"