#!/usr/bin/env bash
# Temporary BOX-3 build fix 1/3:
# lower apps/crypto/mbedtls include priority from -I to -isystem.
#
# Use this only to test the "two mbedTLS trees coexist" route for
# esp32s3-box-3 + smart_home. It modifies apps/crypto/mbedtls/Make.defs in
# the openvela workspace.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
MAKEDEFS="$ROOT_DIR/apps/crypto/mbedtls/Make.defs"

if [ ! -f "$MAKEDEFS" ]; then
    echo "[box3-fix] ERROR: not found: $MAKEDEFS" >&2
    exit 1
fi

echo "[box3-fix] Applying mbedTLS header-priority fix:"
echo "[box3-fix]   $MAKEDEFS"

sed -i 's|CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include|CFLAGS += -isystem $(APPDIR)/crypto/mbedtls/include|' "$MAKEDEFS"
sed -i 's|CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include|CFLAGS += -isystem $(APPDIR)/crypto/mbedtls/mbedtls/include|' "$MAKEDEFS"
sed -i 's|CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/include|CXXFLAGS += -isystem $(APPDIR)/crypto/mbedtls/include|' "$MAKEDEFS"
sed -i 's|CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include|CXXFLAGS += -isystem $(APPDIR)/crypto/mbedtls/mbedtls/include|' "$MAKEDEFS"

echo "[box3-fix] Done."
