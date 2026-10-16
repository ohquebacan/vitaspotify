#!/usr/bin/env bash
# Vita VPK install requires sce_sys PNGs as 8-bit indexed (color type 3).
# A 24-bit RGB icon0.png causes install error 0x8010113D in VitaShell, and the
# LiveArea images that ship with the VitaSDK samples are indexed too, so all
# three go through the same conversion.
#
# Each asset is rebuilt from its *_src.png, which is the one to edit; the
# converted file next to it is a build product. Sizes are the ones the SDK's
# own samples use.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCE="$ROOT/assets/sce_sys"
LA="$SCE/livearea/contents"

# src|out|width|height|required bit depth (0 = any indexed depth)
#
# Only the icon insists on 8 bits: that is the combination this project has
# installed from, and it is the one VitaShell was unhappy about. The LiveArea
# images take whatever depth pngquant lands on -- the bg.png shipped in the
# VitaSDK samples is 1-bit indexed, so a shallow palette is expected, not a
# problem.
ASSETS=(
  "$SCE/icon0_src.png|$SCE/icon0.png|128|128|8"
  "$LA/bg_src.png|$LA/bg.png|840|500|0"
  "$LA/startup_src.png|$LA/startup.png|280|158|0"
)

for cmd in ffmpeg pngquant python3; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Missing $cmd (brew install ffmpeg pngquant)" >&2
    exit 1
  fi
done

TMP="$(mktemp /tmp/vitaspotify_sce.XXXXXX.png)"
cleanup() { rm -f "$TMP"; }
trap cleanup EXIT

for entry in "${ASSETS[@]}"; do
  IFS='|' read -r SRC OUT W H BITS <<< "$entry"
  NAME="$(basename "$OUT")"

  # The source is optional: an asset that is already converted and committed
  # stays as it is, and a LiveArea the project does not ship is not an error.
  if [[ -f "$SRC" ]]; then
    INPUT="$SRC"
  elif [[ -f "$OUT" ]]; then
    INPUT="$OUT"
  else
    continue
  fi

  # pngquant dithers, which matters for bg.png: it is a smooth gradient, and
  # 256 colours without dithering would band visibly.
  ffmpeg -y -i "$INPUT" -vf "scale=$W:$H:flags=lanczos" -pix_fmt rgb24 "$TMP" \
    >/dev/null 2>&1
  pngquant --quality=70-95 --strip --force "$TMP" -o "$OUT"

  python3 - "$OUT" "$W" "$H" "$BITS" <<'PY'
import struct, sys
path, want_w, want_h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
want_bits = int(sys.argv[4])
name = path.rsplit("/", 1)[-1]
with open(path, "rb") as f:
    f.read(8)
    while True:
        ln = f.read(4)
        if not ln:
            break
        length = struct.unpack(">I", ln)[0]
        ctype = f.read(4).decode()
        data = f.read(length)
        f.read(4)
        if ctype == "IHDR":
            w, h, bit, ctype_num, *_ = struct.unpack(">IIBBBBB", data)
            bad_bits = bit > 8 or (want_bits and bit != want_bits)
            if w != want_w or h != want_h or ctype_num != 3 or bad_bits:
                want = f"{want_bits}-bit" if want_bits else "indexed"
                raise SystemExit(
                    f"{name} must be {want_w}x{want_h} {want}, "
                    f"got {w}x{h} bit={bit} type={ctype_num}"
                )
            print(f"{name} OK: {w}x{h} {bit}-bit indexed")
            break
        if ctype == "IEND":
            break
PY
done
