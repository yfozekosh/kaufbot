#!/bin/sh
# fetch_deps.sh — Download third-party dependencies for local development.
# Run this once before building locally: ./fetch_deps.sh

set -e

CJSON_VERSION="v1.7.18"
CJSON_DIR="third_party/cjson"
CJSON_BASE="https://raw.githubusercontent.com/DaveGamble/cJSON/${CJSON_VERSION}"

# ── Helpers ───────────────────────────────────────────────────────────────────

log()  { printf '\033[1;32m[deps]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[deps]\033[0m %s\n' "$*" >&2; exit 1; }

# Prefer wget, fall back to curl
download() {
    url="$1"
    dest="$2"
    if command -v wget >/dev/null 2>&1; then
        wget -q "$url" -O "$dest"
    elif command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$dest"
    else
        err "Neither wget nor curl found. Please install one and retry."
    fi
}

# ── cJSON ─────────────────────────────────────────────────────────────────────

log "Fetching cJSON ${CJSON_VERSION}..."

mkdir -p "$CJSON_DIR"

download "${CJSON_BASE}/cJSON.h" "${CJSON_DIR}/cJSON.h"
download "${CJSON_BASE}/cJSON.c" "${CJSON_DIR}/cJSON.c"

log "cJSON saved to ${CJSON_DIR}/"

# ── Done ──────────────────────────────────────────────────────────────────────

log "All dependencies ready."
log ""
log "Build with:"
log "  cmake -B build -DCJSON_DIR=third_party/cjson"
log "  cmake --build build"
