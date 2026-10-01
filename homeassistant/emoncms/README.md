# Emoncms Feed Migration

Migrates historical data from one Emoncms feed to another using the Emoncms HTTP API.

## Requirements

- Node.js 18+
- Access to an Emoncms instance

## Usage

```bash
# Interactive — prompts for source and target feed IDs
node migrate_feed.js

# Non-interactive
node migrate_feed.js --source 1 --target 43 --yes

# Different server
node migrate_feed.js --base-url https://192.168.1.x:7443 --apikey YOUR_KEY --source 1 --target 43 --yes
```

## Options

| Flag                | Description               | Default                       |
|---------------------|---------------------------|-------------------------------|
| `--source <id>`     | Source feed ID            | prompted                      |
| `--target <id>`     | Target feed ID            | prompted                      |
| `--yes`             | Skip confirmation prompt  | —                             |
| `--base-url <url>`  | Emoncms base URL          | YOUR_EMONCMS_SERVER_HERE:PORT |
| `--apikey <key>`    | Emoncms API key           | YOUR_API_KEY_HERE             |

## How it works

1. Lists all available feeds grouped by tag.
2. Prompts for source and target feed IDs (or reads from `--source`/`--target`).
3. Reads source feed metadata to determine the time range.
4. Fetches data from the source in chunks of up to 8928 points (API limit).
5. Aligns each timestamp to the target feed's fixed interval boundary.
6. Batch-inserts aligned points into the target via `POST /feed/insert.json`.
7. Shows a live progress bar.

## Important notes

- **Clear the target feed before migrating** if it already contains live data with a future `start_time`. Historical inserts before the feed's `start_time` are silently dropped by Emoncms Engine 5 (FIXED).
  Clear via: `GET /feed/clear.json?id=TARGET_ID&apikey=KEY`
- `POST /feed/post.json` is for live data only — it silently drops historical timestamps. This script uses `/feed/insert.json` instead.
- Timestamps are aligned to the nearest multiple of the target interval (e.g. 30 s) to satisfy the FIXED engine's indexing requirement.
- SSL certificate verification is disabled (`NODE_TLS_REJECT_UNAUTHORIZED=0`) for local self-signed certs.
