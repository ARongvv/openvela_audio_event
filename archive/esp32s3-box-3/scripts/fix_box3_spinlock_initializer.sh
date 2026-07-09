#!/usr/bin/env bash
# Temporary BOX-3 build fix 3/3:
# replace ESP HAL's integer spinlock initializer with NuttX SP_UNLOCKED.
#
# Run this in the background during the first build after distclean, or after
# esp-hal-3rdparty has already appeared.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
HAL_DIR="$ROOT_DIR/nuttx/arch/xtensa/src/esp32s3/esp-hal-3rdparty"
CLK_FILE="$HAL_DIR/components/esp_hw_support/clk_ctrl_os.c"

echo "[box3-fix] Waiting for ESP HAL clock control file:"
echo "[box3-fix]   $CLK_FILE"

for _ in $(seq 1 180); do
    if [ -f "$CLK_FILE" ]; then
        break
    fi
    sleep 1
done

if [ ! -f "$CLK_FILE" ]; then
    echo "[box3-fix] ERROR: clk_ctrl_os.c not found after 180s. Is the build running?" >&2
    exit 1
fi

if grep -q '#define LOCK_INITIALIZER_UNLOCKED[[:space:]]*0' "$CLK_FILE"; then
    sed -i 's/#define LOCK_INITIALIZER_UNLOCKED[[:space:]]*0/#define LOCK_INITIALIZER_UNLOCKED       SP_UNLOCKED/' "$CLK_FILE"
    echo "[box3-fix] Replaced LOCK_INITIALIZER_UNLOCKED with SP_UNLOCKED."
else
    echo "[box3-fix] LOCK_INITIALIZER_UNLOCKED is already patched or not present."
fi

echo "[box3-fix] Done."
