#!/usr/bin/env bash
# quick-build-deploy.sh — One-shot: build template + deploy + run on board.
# Parameterized wrapper around build_templates.sh + deploy_run.sh.
#
# Usage:
#   ./quick-build-deploy.sh --board rk3576 --ip 192.168.1.100 \
#     --sdk /path/to/sdk --template cpp-mpp --args "input.h264"
#
# Board capabilities (arch, libs) auto-loaded from boards/registry.yaml.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOARD=""
IP=""
SDK=""
TEMPLATE="c-static"
ARGS=""
USER="root"
PW=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --board) BOARD="$2"; shift 2 ;;
        --ip) IP="$2"; shift 2 ;;
        --sdk) SDK="$2"; shift 2 ;;
        --template) TEMPLATE="$2"; shift 2 ;;
        --args) ARGS="$2"; shift 2 ;;
        --user) USER="$2"; shift 2 ;;
        --password) PW="$2"; shift 2 ;;
        *) echo "unknown: $1"; exit 1 ;;
    esac
done

[[ -n "$BOARD" ]] || { echo "ERROR: --board required (e.g. rk3576)"; exit 1; }
[[ -n "$IP" ]]    || { echo "ERROR: --ip required"; exit 1; }
[[ -n "$SDK" ]]   || { echo "ERROR: --sdk required"; exit 1; }
[[ -n "$PW" ]]    || { echo "ERROR: --password required"; exit 1; }

# Read arch from registry.yaml
REG="$SCRIPT_DIR/../boards/registry.yaml"
[[ -f "$REG" ]] || { echo "registry.yaml not found"; exit 1; }
ARCH=$(python3 -c "
import yaml
with open('$REG') as f:
    r = yaml.safe_load(f)
print(r['boards']['$BOARD']['arch'])
" 2>/dev/null) || { echo "failed to read arch for $BOARD from registry"; exit 1; }

echo "=== board=$BOARD arch=$ARCH template=$TEMPLATE ==="

# 1. Build
"$SCRIPT_DIR/build_templates.sh" --arch "$ARCH" --sdk "$SDK" --template "$TEMPLATE"

# 2. Deploy + run
OUT="$SCRIPT_DIR/../build-out/$TEMPLATE"
"$SCRIPT_DIR/deploy_run.sh" --ip "$IP" --user "$USER" --password "$PW" \
    --local "$OUT" --remote "/tmp/$TEMPLATE" --args "$ARGS"