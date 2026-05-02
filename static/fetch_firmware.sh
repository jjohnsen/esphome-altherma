#!/usr/bin/env bash
set -euo pipefail

OWNER="jjohnsen"
REPO="esphome-altherma"
OUT_DIR="_site/firmware"

mkdir -p "$OUT_DIR"

#curl -sL "https://api.github.com/repos/$OWNER/$REPO/releases/tags/v1.0.4" \
curl -sL "https://api.github.com/repos/$OWNER/$REPO/releases/latest" \
  -H "Authorization: Bearer ${GITHUB_TOKEN}" \
| ruby -r json -e '
data = JSON.parse(STDIN.read)
if data.is_a?(Hash) && data["assets"].is_a?(Array)
  data["assets"].each do |a|
    puts "#{a["browser_download_url"]}|#{a["name"]}"
  end
else
  STDERR.puts "Error: Failed to fetch releases or no assets found. Response: #{data.inspect}"
  exit 1
end
' \
| while IFS='|' read -r url name; do
    curl -L "$url" -o "$OUT_DIR/$name"
  done