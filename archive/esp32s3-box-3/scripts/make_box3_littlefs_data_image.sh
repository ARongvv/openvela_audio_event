#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SMART_HOME_DIR="${REPO_ROOT}/contest2026_031_niudanxianqianchong/demos/smart_home"
OUT_DIR="${REPO_ROOT}/out/box3_littlefs_data"
STAGING_DIR="${OUT_DIR}/staging"

DATA_SIZE="${DATA_SIZE:-0x100000}"
BLOCK_SIZE="${BLOCK_SIZE:-4096}"
PAGE_SIZE="${PAGE_SIZE:-1024}"
OUT_IMAGE="${OUT_IMAGE:-${OUT_DIR}/data_lfs.bin}"
MKLITTLEFS="${MKLITTLEFS:-${REPO_ROOT}/vendor/artinchip/tools/scripts/mklittlefs}"
WITH_ICONS="${WITH_ICONS:-0}"
WITH_FONTS="${WITH_FONTS:-0}"

if [[ ! -x "${MKLITTLEFS}" ]]; then
  echo "mklittlefs not found or not executable: ${MKLITTLEFS}" >&2
  echo "Set MKLITTLEFS=/path/to/mklittlefs and retry." >&2
  exit 1
fi

rm -rf "${STAGING_DIR}"
mkdir -p "${STAGING_DIR}/res/skills"

cp "${SMART_HOME_DIR}"/res/skills/*.md "${STAGING_DIR}/res/skills/"

if [[ "${WITH_ICONS}" == "1" ]]; then
  mkdir -p "${STAGING_DIR}/res/icons"
  cp "${SMART_HOME_DIR}"/res/icons/*.png "${STAGING_DIR}/res/icons/"
fi

if [[ "${WITH_FONTS}" == "1" ]]; then
  mkdir -p "${STAGING_DIR}/res/fonts"
  # Use subset fonts if available, fall back to originals
  for font in MiSans-Normal.ttf MiSans-Semibold.ttf; do
    subset="${SMART_HOME_DIR}/res/fonts/${font%.ttf}-subset.ttf"
    if [[ -f "${subset}" ]]; then
      cp "${subset}" "${STAGING_DIR}/res/fonts/${font}"
    elif [[ -f "${SMART_HOME_DIR}/res/fonts/${font}" ]]; then
      echo "WARNING: using full-size ${font} (run scripts/subset_font.sh first)" >&2
      cp "${SMART_HOME_DIR}/res/fonts/${font}" "${STAGING_DIR}/res/fonts/"
    fi
  done
fi

mkdir -p "$(dirname "${OUT_IMAGE}")"

"${MKLITTLEFS}" \
  -c "${STAGING_DIR}" \
  -b "${BLOCK_SIZE}" \
  -p "${PAGE_SIZE}" \
  -s "${DATA_SIZE}" \
  "${OUT_IMAGE}"

echo "LittleFS image: ${OUT_IMAGE}"
echo "Flash with:"
echo "  esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash 0xE00000 ${OUT_IMAGE}"
