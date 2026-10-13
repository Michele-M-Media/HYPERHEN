#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
master="${repo_root}/source/bootstrapper/assets/logo.png"
output="${repo_root}/source/bootstrapper/assets/hyperhen_sicon.png"

if command -v magick >/dev/null 2>&1; then
  image_tool=(magick)
elif command -v convert >/dev/null 2>&1; then
  image_tool=(convert)
else
  echo "ImageMagick is required" >&2
  exit 1
fi
command -v ffmpeg >/dev/null 2>&1 || { echo "ffmpeg with APNG support is required" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf -- "${work}"' EXIT

scales=(108 110 112 114 116 118 120 118 116 114 112 110)
alphas=(18 22 27 33 40 48 56 48 40 33 27 22)

for index in "${!scales[@]}"; do
  frame="$(printf '%02d' "${index}")"
  scale="${scales[${index}]}"
  alpha="${alphas[${index}]}"
  angle="$((index * 30))"

  "${image_tool[@]}" "${master}" -resize 104x104 \
    -gravity center -background none -extent 128x128 "${work}/core.png"
  "${image_tool[@]}" "${master}" -resize "${scale}x${scale}" \
    -channel A -evaluate multiply "$(awk "BEGIN { print ${alpha}/100 }")" +channel \
    -blur 0x3.2 -gravity center -background none -extent 128x128 "${work}/glow.png"
  "${image_tool[@]}" -size 128x128 xc:none \
    -stroke '#37E9FFAA' -strokewidth 2 -fill none \
    -draw 'arc 8,8 120,120 205,325' \
    -stroke '#D643FF88' -draw 'arc 12,12 116,116 20,125' \
    -rotate "${angle}" -gravity center -background none -extent 128x128 "${work}/orbit.png"
  "${image_tool[@]}" -size 128x128 xc:none \
    "${work}/glow.png" -compose over -composite \
    "${work}/orbit.png" -compose screen -composite \
    "${work}/core.png" -compose over -composite \
    -depth 8 -strip "${work}/frame_${frame}.png"
done

ffmpeg -hide_banner -loglevel error -y -framerate 12 \
  -i "${work}/frame_%02d.png" -pix_fmt rgba -plays 0 -f apng "${output}"
chmod 0644 "${output}"
