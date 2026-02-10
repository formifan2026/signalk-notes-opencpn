#!/usr/bin/env bash
set -e

echo "DEBUG: Running flatpak-package.sh"
echo "DEBUG: Searching for files/ directory under: $1"

FILES_DIR=$(find "$1" -type d -name files | head -n 1)

echo "DEBUG: Found FILES_DIR: $FILES_DIR"

if [ -z "$FILES_DIR" ]; then
    echo "ERROR: Could not locate Flatpak output directory (files/)"
    exit 1
fi

echo "DEBUG: Running TAR..."
tar -czf "$2" \
    --verbose \
    --transform="s|.*/files/|$3/|" \
    "$FILES_DIR"

echo "DEBUG: TAR completed successfully."
