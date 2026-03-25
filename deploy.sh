#!/bin/bash
# Deploy script for Kaufbot
# Usage: ./deploy.sh
set -e

STACK_DIR="/opt/stacks/kaufbot"

echo "Building kaufbot..."
docker compose build

# Create stack directory if it doesn't exist
if [ ! -d "$STACK_DIR" ]; then
    echo "Creating stack directory $STACK_DIR..."
    sudo mkdir -p "$STACK_DIR"
    sudo mkdir -p "$STACK_DIR/data"

    cat > /tmp/kaufbot-compose.yml <<'EOF'
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

    sudo mv /tmp/kaufbot-compose.yml "$STACK_DIR/docker-compose.yml"
    sudo chown -R "$USER":"$USER" "$STACK_DIR"

    if [ -f .env ]; then
        cp .env "$STACK_DIR/"
    elif [ -f .env.example ]; then
        cp .env.example "$STACK_DIR/.env"
        echo "Warning: copied .env.example — edit $STACK_DIR/.env before starting."
    fi
fi

pushd "$STACK_DIR"
docker compose down
docker compose up -d
popd

echo "Done. kaufbot is running."
