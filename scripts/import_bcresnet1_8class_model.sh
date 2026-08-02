#!/usr/bin/env bash
# Generate the preflight model source from the verified recalibrated TFLite.

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 /path/to/model_int8.tflite" >&2
    exit 2
fi

INPUT="$1"
EXPECTED_SHA256="37834bb5beb39d1872dd279a8c99b1feff0d071e780ec5c6fc29a15a83731148"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODEL_DIR="$(cd "$SCRIPT_DIR/../app/audio_event/model" && pwd)"
OUTPUT="$MODEL_DIR/bcresnet1_8class_model.cc"

if [ ! -f "$INPUT" ]; then
    echo "[bcresnet-import] ERROR: missing input model: $INPUT" >&2
    exit 1
fi

ACTUAL_SHA256="$(sha256sum "$INPUT" | awk '{print $1}')"
if [ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]; then
    echo "[bcresnet-import] ERROR: unexpected model SHA-256: $ACTUAL_SHA256" >&2
    exit 1
fi

TEMP_OUTPUT="$(mktemp "$MODEL_DIR/.bcresnet1_8class_model.XXXXXX")"
trap 'rm -f "$TEMP_OUTPUT"' EXIT

{
    printf '// Generated from model_int8.tflite; do not edit.\n'
    printf '#include "bcresnet1_8class_model.h"\n\n'
    xxd -i "$INPUT" | \
        sed -e '1c\alignas(16) const unsigned char g_bcresnet1_8class_model[] = {' \
            -e 's/^unsigned int .* = /const unsigned int g_bcresnet1_8class_model_len = /'
} > "$TEMP_OUTPUT"

mv "$TEMP_OUTPUT" "$OUTPUT"
trap - EXIT
echo "[bcresnet-import] Generated $OUTPUT"
