#!/usr/bin/env bash
# Deploys this App Lab project to the UNO Q over the network and (re)starts it.
#
# Usage: scripts/deploy.sh [board-user@host]
# Defaults to $BOARD_HOST, or arduino@10.0.0.195 if unset.
#
# Requires SSH key-based access to the board (password auth doesn't work
# non-interactively) -- run scripts/setup_ssh_key.py once to set this up.

set -euo pipefail

BOARD_IP="${1:-${BOARD_IP:-10.0.0.195}}"
BOARD_HOST="${1:-${BOARD_HOST:-arduino@${BOARD_IP}}}"
# SSH-based reachability check (not ping): portable across shells where `ping` may
# resolve to a non-POSIX binary (e.g. Windows ping.exe, whose `-c` means "compartment"
# and requires admin rights rather than "count"), and it tests the thing that actually
# matters -- SSH access -- rather than raw ICMP, which some networks block anyway.
ssh -o ConnectTimeout=5 -o BatchMode=yes "$BOARD_HOST" true 2>/dev/null || { echo "==> ERROR: Board not reachable via SSH at ${BOARD_HOST} (check BOARD_IP/BOARD_HOST, network, or run scripts/setup_ssh_key.py)"; exit 1; }
APP_NAME="smoothsensors03"
REMOTE_DIR="ArduinoApps/${APP_NAME}"
LOCAL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> Syncing code to ${BOARD_HOST}:${REMOTE_DIR}"
tar czf - -C "$LOCAL_DIR" \
  --exclude='python/model' \
  --exclude='python/__pycache__' \
  sketch python app.yaml \
  | ssh "$BOARD_HOST" "mkdir -p ${REMOTE_DIR} && tar xzf - -C ${REMOTE_DIR}"

if ssh "$BOARD_HOST" "test -d ${REMOTE_DIR}/python/model/am"; then
  echo "==> Vosk model already present on board, skipping (delete ${REMOTE_DIR}/python/model there to force a re-upload)"
else
  echo "==> Vosk model not found on board, uploading python/model/ (this is large and may take a while)"
  tar czf - -C "$LOCAL_DIR/python" model \
    | ssh "$BOARD_HOST" "mkdir -p ${REMOTE_DIR}/python && tar xzf - -C ${REMOTE_DIR}/python"
fi

echo "==> Restarting the app (compiles + uploads the sketch, restarts the Python app)"
if ! ssh "$BOARD_HOST" "arduino-app-cli app restart ${REMOTE_DIR}"; then
  echo "==> restart failed (arduino-app-cli sometimes leaves the app stopped instead of restarting it), falling back to stop + start"
  ssh "$BOARD_HOST" "arduino-app-cli app stop ${REMOTE_DIR}" || true
  ssh "$BOARD_HOST" "arduino-app-cli app start ${REMOTE_DIR}"
fi

echo "==> Done. Tail logs with: ssh ${BOARD_HOST} arduino-app-cli app logs ${REMOTE_DIR} --follow"
