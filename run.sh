#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Telegram Bot Configuration
# Get your bot token from @BotFather on Telegram
# export TELEGRAM_TOKEN="8464912658:AAFaBrR9oWgDENTlg5trngJ5tk7CDRRNVQUc>
#
# # Google Gemini API Configuration
# # Get your API key from https://makersuite.google.com/app/apikey
# export GEMINI_API_KEY="AIzaSyBBl5HzFmQSTIqAaUcOS8Nb0jGvxFKLvjE"
#
# # Allowed Telegram User IDs (comma-separated)
# # Find your user ID using @userinfobot on Telegram
# export ALLOWED_USER_IDS="8190702515,380108276"
#
# # Optional Configuration
# export GEMINI_MODEL="gemini-2.5-flash"
# export STORAGE_PATH="${SCRIPT_DIR}/data/files"
# export DB_PATH="${SCRIPT_DIR}/data/bot.db"
#
# echo "[run.sh] STORAGE_PATH=$STORAGE_PATH"
# echo "[run.sh] DB_PATH=$DB_PATH"
#

source ./.env

# Run the bot
cd "${SCRIPT_DIR}/build"
exec ./tgbot
