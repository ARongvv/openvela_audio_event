#!/usr/bin/env bash
# Check or create the optional TFLM ESP-NN adapter source link.
#
# The default mode intentionally does not modify apps/.  This lets a project
# checkout keep the adapter source under ccf_audioevent while an existing
# working ESP-NN integration remains untouched.

set -euo pipefail

usage()
{
  cat <<'EOF'
Usage: link_tflm_espnn_adapter.sh [--check|--link]

  --check  Verify that the ccf_audioevent adapter sources are present and
           report the state of apps/mlearning/tflite-micro/kernels/esp_nn.
           This is the default and never changes apps/.
  --link   Create apps/mlearning/tflite-micro/kernels/esp_nn as a symbolic
           link when that path does not already exist.  Existing files,
           directories, and unexpected links are always left untouched.
EOF
}

mode=check
if [ "$#" -gt 1 ]; then
  usage >&2
  exit 2
fi

if [ "$#" -eq 1 ]; then
  case "$1" in
    --check) mode=check ;;
    --link) mode=link ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
ccf_root="$(cd "$script_dir/.." && pwd)"
openvela_root="${OPENVELA_ROOT:-$(cd "$ccf_root/.." && pwd)}"
source_dir="$ccf_root/third_party/tflm-espnn-adapter/kernels"
link_path="$openvela_root/apps/mlearning/tflite-micro/kernels/esp_nn"

files=(conv.cc depthwise_conv.cc mean.cc micro_time_esp32s3.cc)
for file in "${files[@]}"; do
  if [ ! -f "$source_dir/$file" ]; then
    echo "[tflm-espnn-adapter] ERROR: missing source: $source_dir/$file" >&2
    exit 1
  fi
done

source_real="$(realpath "$source_dir")"

if [ -L "$link_path" ]; then
  link_real="$(readlink -f "$link_path" || true)"
  if [ "$link_real" = "$source_real" ]; then
    echo "[tflm-espnn-adapter] link is correct: $link_path"
    exit 0
  fi

  echo "[tflm-espnn-adapter] ERROR: refusing to replace unexpected link:" >&2
  echo "  $link_path -> $(readlink "$link_path")" >&2
  exit 1
fi

if [ -e "$link_path" ]; then
  matches=1
  for file in "${files[@]}"; do
    if [ ! -f "$link_path/$file" ] || ! cmp -s "$source_dir/$file" "$link_path/$file"; then
      matches=0
      break
    fi
  done

  if [ "$matches" -eq 1 ]; then
    echo "[tflm-espnn-adapter] existing non-link adapter matches ccf source: $link_path"
    echo "[tflm-espnn-adapter] not replacing an existing path; use --link only after moving it aside yourself."
    exit 0
  else
    echo "[tflm-espnn-adapter] existing non-link adapter differs: $link_path" >&2
    echo "[tflm-espnn-adapter] refusing to replace or link over an existing path." >&2
    exit 1
  fi
fi

if [ "$mode" = check ]; then
  echo "[tflm-espnn-adapter] link is absent: $link_path"
  echo "[tflm-espnn-adapter] run with --link to create it."
  exit 0
fi

mkdir -p "$(dirname "$link_path")"
link_target="$(realpath --relative-to="$(dirname "$link_path")" "$source_real")"
ln -s "$link_target" "$link_path"
echo "[tflm-espnn-adapter] created: $link_path -> $link_target"
