#!/bin/bash
# MU Online Remaster — Model Viewer launch script

set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
CLIENT_DIR="$ROOT/client/build"
VIEWER_BIN="$CLIENT_DIR/ModelViewer"

# ── Colors ──
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# ── Verify Data symlink ──
if [ ! -L "$CLIENT_DIR/Data" ]; then
    echo -e "${YELLOW}[ModelViewer] Creating Data symlink...${NC}"
    ln -sfn "$ROOT/client/Data" "$CLIENT_DIR/Data"
fi

# ── Build if needed ──
if [ ! -f "$VIEWER_BIN" ]; then
    echo -e "${YELLOW}[ModelViewer] Binary not found, building...${NC}"
    cd "$CLIENT_DIR" && cmake -DCMAKE_BUILD_TYPE=Release .. && ninja
fi

# ── Launch ──
echo -e "${GREEN}[ModelViewer] Starting...${NC}"
cd "$CLIENT_DIR"
./ModelViewer
