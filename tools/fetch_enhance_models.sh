#!/usr/bin/env bash
# Downloads the two optional Enhance models and checks them: fetch_enhance_models.sh <output folder>
# Result: <out>/restore (Restore + its license) and <out>/sidon (Sidon + its license).
set -euo pipefail
out="$1"; here="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$out/restore" "$out/sidon"
sum() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
# get <file> <sha256> <url> [<url> ...]: the first address that delivers the expected file wins
get() {
  local file="$1" want="$2"; shift 2
  for url in "$@"; do
    echo "fetching $url"
    if curl -fL --retry 3 --retry-delay 5 -sS -o "$file" "$url"; then
      local have; have="$(sum "$file")"
      if [ "$have" = "$want" ]; then echo "ok $(basename "$file")"; return 0; fi
      echo "checksum differs for $url: $have"
    fi
  done
  echo "could not get $(basename "$file")"; return 1
}
get "$out/restore/restore.fp16.onnx" 46eb6ebb05cc50bb6c3b1af87e3c4542bf30cedbcc356e2c68646d3bbf9a65f1 \
  https://huggingface.co/audiojs/restore/resolve/main/restore.fp16.onnx \
  https://huggingface.co/audiojs/restore/resolve/main/restore_fp16.onnx
curl -fL --retry 3 -sS -o "$out/restore/restore-LICENSE-OpenRAIL.txt" https://huggingface.co/audiojs/restore/resolve/main/LICENSE
grep -q "Use Restrictions" "$out/restore/restore-LICENSE-OpenRAIL.txt"
cp "$here/resources/models/ENHANCE-NOTICES.txt" "$out/restore/"
for f in sidon-predictor sidon-vocoder; do
  case $f in
    sidon-predictor) want=ff50dc41f309903ac5dc071afb969a3f950b15e77f7e226222fd0011e76910c7 ;;
    *)               want=b2598f7fae8913ee42a4ddedc4bb50ccd76f15546ec1f93520c19187f4a33644 ;;
  esac
  get "$out/sidon/$f.onnx" "$want" \
    "https://huggingface.co/soniqo/Sidon-ONNX/resolve/main/int8/$f.onnx" \
    "https://huggingface.co/aufklarer/Sidon-ONNX/resolve/main/int8/$f.onnx"
done
cp "$here/resources/models/Sidon-LICENSE-MIT.txt" "$here/resources/models/ENHANCE-NOTICES.txt" "$out/sidon/"
ls -la "$out/restore" "$out/sidon"
