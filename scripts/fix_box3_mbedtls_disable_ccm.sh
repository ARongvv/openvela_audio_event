#!/usr/bin/env bash
# Temporary BOX-3 build fix 2/3:
# disable MBEDTLS_CCM_C in ESP HAL's generated mbedtls_config.h.
#
# Run this in the background during the first build after distclean, or after
# esp-hal-3rdparty has already appeared. The script waits for the HAL mbedTLS
# config file because openvela may clone/patch esp-hal-3rdparty during build.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
HAL_DIR="$ROOT_DIR/nuttx/arch/xtensa/src/esp32s3/esp-hal-3rdparty"
MBEDTLS_CFG="$HAL_DIR/components/mbedtls/mbedtls/include/mbedtls/mbedtls_config.h"

echo "[box3-fix] Waiting for ESP HAL mbedTLS config:"
echo "[box3-fix]   $MBEDTLS_CFG"

for _ in $(seq 1 180); do
    if [ -f "$MBEDTLS_CFG" ]; then
        break
    fi
    sleep 1
done

if [ ! -f "$MBEDTLS_CFG" ]; then
    echo "[box3-fix] ERROR: mbedtls_config.h not found after 180s. Is the build running?" >&2
    exit 1
fi

if grep -q '^#define MBEDTLS_CCM_C$' "$MBEDTLS_CFG"; then
    sed -i 's/^#define MBEDTLS_CCM_C$/\/\* #define MBEDTLS_CCM_C \*\//' "$MBEDTLS_CFG"
    echo "[box3-fix] Disabled MBEDTLS_CCM_C."
else
    echo "[box3-fix] MBEDTLS_CCM_C is already disabled or not present."
fi

echo "[box3-fix] Done."
