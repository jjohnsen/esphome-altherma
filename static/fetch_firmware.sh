#!/usr/bin/env bash
set -euo pipefail

OWNER="jjohnsen"
REPO="esphome-altherma"
OUT_DIR="_site/firmware"

mkdir -p "$OUT_DIR"

curl -sL "https://api.github.com/repos/$OWNER/$REPO/releases/latest" \
| ruby -r json -e '
data = JSON.parse(STDIN.read)
data["assets"].each do |a|
  puts "#{a["browser_download_url"]}|#{a["name"]}"
end
' \
| while IFS='|' read -r url name; do
    curl -L "$url" -o "$OUT_DIR/$name"
  done