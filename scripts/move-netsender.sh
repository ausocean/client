#!/bin/bash

# Directories
SRC_DIR="../arduino/netsender"
DEST_DIR="../arduino/temp-netsender"

# Files to sync (excluding nonarduino.h)
FILES_TO_SYNC=(
    "handlers.cpp"
    "NetSender.cpp"
    "NetSender.h"
    "offline.cpp"
    "online.cpp"
)

# Step 1: Delete old versions in temp-netsender
for file in "${FILES_TO_SYNC[@]}"; do
    DEST_PATH="$DEST_DIR/$file"
    if [ -f "$DEST_PATH" ]; then
        rm "$DEST_PATH"
        echo "Deleted: $DEST_PATH"
    else
        echo "Not found (skipped delete): $DEST_PATH"
    fi
done

# Step 2: Copy files from netsender to temp-netsender
for file in "${FILES_TO_SYNC[@]}"; do
    SRC_PATH="$SRC_DIR/$file"
    DEST_PATH="$DEST_DIR/$file"
    if [ -f "$SRC_PATH" ]; then
        cp "$SRC_PATH" "$DEST_PATH"
        echo "Copied: $SRC_PATH → $DEST_PATH"
    else
        echo "Source file missing: $SRC_PATH"
    fi
done
