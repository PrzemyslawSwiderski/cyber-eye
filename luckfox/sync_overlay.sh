#!/bin/bash

# Configuration
LUCKFOX_IP="192.168.1.15" # Change this to your Luckfox IP
LUCKFOX_USER="root"       # Usually root
LUCKFOX_PASS="luckfox"
LOCAL_OVERLAY="./overlay/" # Your local overlay path

# SSH options to skip host key check and auto-answer
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"

# Check for dry-run parameter
if [ "$1" = "dry" ]; then
	DRY_RUN="--dry-run -i" # -i shows itemized changes
	echo "=== DRY RUN MODE - No files will be changed ==="
else
	DRY_RUN=""
fi

# Sync overlay to Luckfox root
sshpass -p "$LUCKFOX_PASS" rsync -avz $DRY_RUN \
	-e "ssh $SSH_OPTS" \
	--exclude='/proc' \
	--exclude='/sys' \
	--exclude='/dev' \
	--exclude='/tmp' \
	--exclude='/run' \
	"${LOCAL_OVERLAY}" \
	"${LUCKFOX_USER}@${LUCKFOX_IP}:/"

# Only reboot if not dry-run
if [ "$1" != "dry" ]; then
	echo "Sync complete."
else
	echo "=== Dry run complete - No changes made ==="
fi
