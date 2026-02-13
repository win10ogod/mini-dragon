# minidragon

A minimal C++20 AI agent framework with tool-calling, cron scheduling, and multi-channel gateway.

## Build

Requirements: CMake 3.20+, C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)

Dependencies are automatically downloaded via CMake FetchContent:
- nlohmann/json (JSON parsing)
- cpp-httplib (HTTP server/client)
- sqlite3 (cron job storage)

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

## Quick Start

### 1. Initialize
```bash
./minidragon onboard
```
Creates `~/.minidragon/config.json` and workspace directories.

### 2. Configure
Edit `~/.minidragon/config.json`:
```json
{
  "agents": { "defaults": {
    "workspace": "~/.minidragon/workspace",
    "model": "gpt-4.1-mini",
    "max_tokens": 2048,
    "temperature": 0.7,
    "max_tool_iterations": 20
  }},
  "providers": {
    "openai_compat": { "api_key": "sk-...", "api_base": "http://127.0.0.1:8000/v1" }
  },
  "channels": {
    "cli": { "enabled": true },
    "http": { "enabled": true }
  },
  "tools": {
    "spawn": { "enabled": true, "allowlist": ["git","ls","dir","cat","type"] }
  }
}
```

### 3. Run Agent (Interactive)
```bash
./minidragon agent
```
Or single message:
```bash
./minidragon agent -m "Hello, what tools do you have?"
```

### 4. Start Gateway
```bash
./minidragon gateway --host 127.0.0.1 --port 18790
```

### 5. Test HTTP /chat endpoint
```bash
curl -X POST http://127.0.0.1:18790/chat \
  -H "Content-Type: application/json" \
  -d '{"channel":"http","user":"test","text":"Hello!"}'
```

Health check:
```bash
curl http://127.0.0.1:18790/health
```

### 6. Check Status
```bash
./minidragon status
```

## Cron Jobs

Add a periodic job:
```bash
./minidragon cron add --name "reminder" --message "Check emails" --every 3600
```

Add a cron-scheduled job:
```bash
./minidragon cron add --name "morning" --message "Good morning summary" --cron "0 9 * * *"
```

List jobs:
```bash
./minidragon cron list
```

Remove a job:
```bash
./minidragon cron remove 1
```

## Skills

Place skill JSON files in `~/.minidragon/workspace/skills/`. Each skill file defines:

```json
{
  "name": "my_skill",
  "description": "Description of the skill",
  "schema": { "type": "object", "properties": {} },
  "type": "tool"
}
```

- `type: "tool"` - registered as a stub tool (returns "not implemented" until connected)
- `type: "prompt"` - loaded as a prompt skill (no tool registration)

## Architecture

- **Provider**: OpenAI-compatible Chat Completions client (supports native tool_calls + fallback `<toolcall>` tag parsing)
- **Agent Loop**: system prompt -> tool iterations (max configurable) -> final reply
- **Tools**: Built-in `spawn` (allowlisted commands) and `cron` (job management) + skill stubs
- **Channels**: CLI (stdin/stdout), HTTP (/chat, /health), stubs for Telegram/Discord/Slack
- **Cron**: SQLite-backed storage, background polling thread in gateway mode
- **Sessions**: JSONL logs in `~/.minidragon/workspace/sessions/`

## Workspace Structure

```
~/.minidragon/
├── config.json
└── workspace/
    ├── AGENTS.md
    ├── IDENTITY.md
    ├── TOOLS.md
    ├── USER.md
    ├── sessions/
    │   └── 2025-01-01.jsonl
    ├── memory/
    ├── cron/
    │   └── cron.db
    └── skills/
        └── *.json
```

## License

MIT
