#!/usr/bin/env bash
#
# Build the static LittleFS resource image used by audio_event ESP-NN profiles.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEFAULT_RESOURCE_DIR="${PROJECT_DIR}/app/audio_event/res/audio"
OUT_DIR="${PROJECT_DIR}/out/audio_event_littlefs"
OUT_IMAGE="${OUT_DIR}/audio_event_littlefs.bin"

# Keep these values in sync with the audio_event_*_espnn profile defconfigs.
FLASH_SIZE=0x1000000
DATA_OFFSET=0x180000
DATA_SIZE=0x800000
BLOCK_SIZE=4096
PAGE_SIZE=1024
MKLITTLEFS="${MKLITTLEFS:-mklittlefs}"

die()
{
  echo "[audio-event-littlefs] error: $*" >&2
  exit 1
}

if [[ "${MKLITTLEFS}" == */* ]]; then
  [[ -x "${MKLITTLEFS}" ]] || die "MKLITTLEFS is not executable: ${MKLITTLEFS}"
else
  command -v "${MKLITTLEFS}" >/dev/null || \
    die "mklittlefs was not found; set MKLITTLEFS=/absolute/path/to/mklittlefs"
fi

if ((DATA_OFFSET + DATA_SIZE > FLASH_SIZE)); then
  die "LittleFS range exceeds the 16 MiB flash"
fi

mkdir -p "${OUT_DIR}"
STAGING_DIR="$(mktemp -d "${TMPDIR:-/tmp}/audio_event_littlefs.XXXXXX")"
trap 'rm -rf -- "${STAGING_DIR}"' EXIT

if (($# == 0)); then
  set -- "${DEFAULT_RESOURCE_DIR}"
fi

copied=0
for source in "$@"; do
  if [[ -f "${source}" ]]; then
    case "${source}" in
      *.wav|*.WAV) ;;
      *) die "resource file is not a WAV: ${source}" ;;
    esac

    target="${STAGING_DIR}/$(basename "${source}")"
    [[ ! -e "${target}" ]] || die "duplicate WAV filename: $(basename "${source}")"
    cp -- "${source}" "${target}"
    copied=$((copied + 1))
  elif [[ -d "${source}" ]]; then
    found=0
    for wav in "${source}"/*.wav "${source}"/*.WAV; do
      [[ -e "${wav}" ]] || continue
      target="${STAGING_DIR}/$(basename "${wav}")"
      [[ ! -e "${target}" ]] || die "duplicate WAV filename: $(basename "${wav}")"
      cp -- "${wav}" "${target}"
      copied=$((copied + 1))
      found=1
    done
    ((found)) || die "no top-level WAV file found in: ${source}"
  else
    die "resource path does not exist: ${source}"
  fi
done

((copied)) || die "no WAV resource was copied"

"${MKLITTLEFS}" -c "${STAGING_DIR}" -b "${BLOCK_SIZE}" -p "${PAGE_SIZE}" \
  -s "${DATA_SIZE}" "${OUT_IMAGE}"

image_size="$(stat -c '%s' "${OUT_IMAGE}")"
expected_size=$((DATA_SIZE))
[[ "${image_size}" -eq "${expected_size}" ]] || \
  die "image size ${image_size} does not equal ${expected_size}"

echo "[audio-event-littlefs] image: ${OUT_IMAGE}"
echo "[audio-event-littlefs] WAV files: ${copied}, image size: ${image_size} bytes"
echo "[audio-event-littlefs] flash offset: $(printf '0x%x' "${DATA_OFFSET}")"
echo "[audio-event-littlefs] example:"
printf '%s\n' '  esptool --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 write-flash \'
printf '    0x%x %s\n' "${DATA_OFFSET}" "${OUT_IMAGE}"
