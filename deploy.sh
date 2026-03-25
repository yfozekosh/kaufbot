#!/bin/bash
# Deploy script for Kaufbot
# Usage: ./deploy.sh
# Typical flow: git pull && ./deploy.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STACK_DIR="/opt/stacks/kaufbot"

echo "Building kaufbot..."
cd "$SCRIPT_DIR"
docker compose build

# Create stack directory if it doesn't exist
if [ ! -d "$STACK_DIR" ]; then
    echo "Creating stack directory $STACK_DIR..."
    sudo mkdir -p "$STACK_DIR"
    sudo mkdir -p "$STACK_DIR/data"

    cat > "$STACK_DIR/docker-compose.yml" <<'EOF'
services:
  kaufbot:
    image: kaufbot:latest
    container_name: kaufbot
    restart: unless-stopped
    env_file: .env
    volumes:
      - ./data:/data
    logging:
      driver: json-file
      options:
        max-size: 10m
        max-file: "3"
networks: {}
EOF

    sudo chown -R "$USER":"$USER" "$STACK_DIR"

    if [ -f "$SCRIPT_DIR/.env" ]; then
        cp "$SCRIPT_DIR/.env" "$STACK_DIR/"
    elif [ -f "$SCRIPT_DIR/.env.example" ]; then
        cp "$SCRIPT_DIR/.env.example" "$STACK_DIR/.env"
        echo "Warning: copied .env.example — edit $STACK_DIR/.env before starting."
    fi
fi

pushd "$STACK_DIR"
docker compose down
docker compose up -d
popd

echo "Done. kaufbot is running."
