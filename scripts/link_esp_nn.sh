#!/usr/bin/env bash
# Create the local ESP-NN source link consumed by the openvela TFLM build.

set -euo pipefail

if [ "$#" -ne 0 ]; then
    echo "Usage: $0" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CCF_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="${OPENVELA_ROOT:-$(cd "$CCF_ROOT/.." && pwd)}"
SOURCE_DIR="$CCF_ROOT/third_party/esp-nn"
LINK_PARENT="$OPENVELA_ROOT/apps/mlearning/esp-nn"
LINK_PATH="$LINK_PARENT/esp-nn"

if [ ! -f "$SOURCE_DIR/LICENSE" ] || [ ! -d "$SOURCE_DIR/include" ] || \
   [ ! -d "$SOURCE_DIR/src" ]; then
    echo "[esp-nn-link] ERROR: vendored ESP-NN source is incomplete: $SOURCE_DIR" >&2
    exit 1
fi

if [ ! -d "$OPENVELA_ROOT/apps/mlearning/tflite-micro" ]; then
    echo "[esp-nn-link] ERROR: not an openvela root: $OPENVELA_ROOT" >&2
    echo "[esp-nn-link] Set OPENVELA_ROOT to the openvela checkout if needed." >&2
    exit 1
fi

SOURCE_REAL="$(realpath "$SOURCE_DIR")"

if [ -L "$LINK_PATH" ]; then
    LINK_REAL="$(readlink -f "$LINK_PATH" || true)"
    if [ "$LINK_REAL" = "$SOURCE_REAL" ]; then
        echo "[esp-nn-link] ESP-NN link is already correct: $LINK_PATH"
        exit 0
    fi

    echo "[esp-nn-link] ERROR: refusing to replace unexpected link:" >&2
    echo "[esp-nn-link]   $LINK_PATH -> $(readlink "$LINK_PATH")" >&2
    exit 1
fi

if [ -e "$LINK_PATH" ]; then
    echo "[esp-nn-link] ERROR: refusing to replace existing path: $LINK_PATH" >&2
    exit 1
fi

mkdir -p "$LINK_PARENT"
LINK_TARGET="$(realpath --relative-to="$LINK_PARENT" "$SOURCE_REAL")"
ln -s "$LINK_TARGET" "$LINK_PATH"

echo "[esp-nn-link] Created: $LINK_PATH -> $LINK_TARGET"
echo "[esp-nn-link] Reconfigure the build before enabling CONFIG_TFLITEMICRO_ESP_NN."
