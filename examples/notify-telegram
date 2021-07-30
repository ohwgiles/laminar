#!/bin/bash -e

# Sends a message from a specified bot to a specific telegram chat ID.
# See https://core.telegram.org/bots

# IMPORTANT: modify this to your real bot token and chat ID
BOT_TOKEN=123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11
CHAT_ID=10000000

LAMINAR_URL=${LAMINAR_BASE_URL:-http://localhost:8080}

[[ $(curl -sS https://api.telegram.org/bot$BOT_TOKEN/sendMessage \
	-d chat_id=$CHAT_ID \
	-d parse_mode=HTML \
	-d text="<a href=\"$LAMINAR_URL/jobs/$JOB/$RUN\">$JOB #$RUN</a> $RESULT" \
	| jq .ok) == true ]]


