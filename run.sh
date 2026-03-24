#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

source ./.env

# Run the bot
cd "${SCRIPT_DIR}/build"
exec ./tgbot
