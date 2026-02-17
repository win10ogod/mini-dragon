# minidragon

A minimal C++20 AI agent framework with tool-calling, multi-provider fallback, hybrid memory search, hook middleware, cron scheduling, team orchestration, and multi-channel gateway.

## Build

Requirements: CMake 3.20+, C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)

Dependencies are automatically downloaded via CMake FetchContent:
- nlohmann/json (JSON parsing)
- cpp-httplib (HTTP server/client)
- sqlite3 (cron jobs, FTS5 memory search, session storage)

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

Cross-compile for Windows (from Linux with mingw):
```bash
mkdir build-win && cd build-win
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64.cmake
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
  "model": "gpt-4.1-mini",
  "workspace": "~/.minidragon/workspace",
  "max_tokens": 2048,
  "temperature": 0.7,
  "max_iterations": 20,
  "context_tokens": 128000,
  "auto_compact": true,
  "providers": {
    "default": { "api_key": "sk-...", "api_base": "https://api.openai.com/v1" },
    "gemini":  { "api_key": "...",    "api_base": "https://generativelanguage.googleapis.com/v1beta/openai" },
    "local":   { "api_base": "http://127.0.0.1:8000/v1" }
  },
  "fallback": {
    "enabled": true,
    "provider_order": ["default", "gemini", "local"],
    "rate_limit_cooldown": 60,
    "billing_cooldown": 18000,
    "auth_cooldown": 3600,
    "timeout_cooldown": 30
  },
  "embedding": {
    "enabled": true,
    "provider": "default",
    "model": "text-embedding-3-small",
    "dimensions": 1536
  },
  "hooks": [
    { "type": "pre_tool_call", "command": "python3 ~/.minidragon/hooks/log_tools.py", "priority": 0 }
  ],
  "tools": {
    "exec": { "allowlist": ["git", "ls", "cat", "dir", "type"] }
  },
  "channels": {
    "http": { "enabled": true },
    "telegram": { "enabled": false, "token": "" }
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

Chat commands inside interactive mode:
- `/new [model]` — Reset session (optionally switch model)
- `/status` — Show provider, token usage, hook count, embedding status
- `/model <name>` — Switch model
- `/context` — Show context window breakdown
- `/compact` — Force LLM-based context compaction
- `/tools` — List available tools
- `/help` — Show help

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

### 6. Check Status
```bash
./minidragon status
```

## Features

### Multi-Provider Fallback Chain

Configure multiple providers and the agent automatically falls through on failure:

1. Tries providers in `fallback.provider_order`
2. Skips providers in cooldown (per error type: rate limit, billing, auth, timeout)
3. Cooldown durations are configurable per error category
4. When fallback is disabled (default), behaves as a single-provider setup

### Schema Adapter

Tool parameter schemas are automatically adapted per provider:
- **Gemini**: Strips unsupported keys (`default`, `$schema`, `additionalProperties`, `title`, `examples`, `format` for non-string types). Converts `anyOf`/`oneOf` to first variant.
- **Anthropic**: Pass-through (handles OpenAI format natively)
- **OpenAI/generic**: Ensures `type:"object"` root, adds `strict:false`

Detection is automatic based on `api_base` URL substring matching.

### LLM-Based Context Compaction

When context approaches the token budget, old messages are summarized:

1. An LLM call generates a concise summary preserving key decisions, file paths, and action items
2. Old messages are replaced with the summary
3. If the LLM call fails, falls back to structural text truncation (head+tail preview)

### Hybrid Memory Search

Two memory tools work together:

- **`memory`** — Save/recall daily and long-term memories (file-based)
- **`memory_search`** — Search across all saved memories using hybrid FTS5 + vector search

Hybrid scoring: `0.7 × vector_score + 0.3 × FTS5_score`

When embedding is enabled, saves are automatically indexed with vector embeddings. When disabled, FTS5 text search is still available.

### Hook System

22 hook types with priority-based execution:

| Category | Hooks |
|----------|-------|
| Agent lifecycle | `agent_start`, `agent_stop` |
| Messages | `pre_tool_call`, `post_tool_call`, `pre_api_call`, `post_api_call`, `pre_user_message`, `post_assistant_message` |
| Context | `pre_compaction`, `post_compaction`, `pre_prune`, `post_prune` |
| Memory | `pre_memory_save`, `post_memory_save`, `pre_memory_search`, `post_memory_search` |
| Provider | `pre_provider_select`, `post_provider_error` |
| Team | `pre_team_message`, `post_team_message` |
| Session | `session_start`, `session_end` |

**Modifying hooks** (pre_*) can alter data flowing through the pipeline. **Void hooks** (post_*) observe without modification.

Shell hooks receive JSON on stdin and return modified JSON on stdout.

### Team Orchestration

Spawn and coordinate multiple agent instances:
```bash
# Lead agent creates a team
./minidragon agent -m "Create a team to refactor the auth module"
```

Tools: `team_create`, `team_spawn`, `team_send`, `team_shutdown`, `team_cleanup`, `team_status`, `inbox_check`, `task_create`, `task_update`, `task_list`

### MCP (Model Context Protocol) Servers

Connect external tool servers via stdio or HTTP:
```json
{
  "mcp_servers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path"]
    },
    "remote": {
      "url": "http://localhost:3001",
      "headers": { "Authorization": "Bearer ..." }
    }
  }
}
```

MCP tools are registered as `mcp_{server}_{tool}`.

## Cron Jobs

```bash
# Add periodic job
./minidragon cron add --name "reminder" --message "Check emails" --every 3600

# Add cron-scheduled job
./minidragon cron add --name "morning" --message "Good morning summary" --cron "0 9 * * *"

# List / remove
./minidragon cron list
./minidragon cron remove 1
```

## Skills

Place skill JSON files in `~/.minidragon/workspace/skills/`:

```json
{
  "name": "my_skill",
  "description": "Description of the skill",
  "schema": { "type": "object", "properties": {} },
  "type": "tool"
}
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Agent                                          │
│  ┌───────────┐  ┌──────────────┐  ┌──────────┐ │
│  │ HookRunner│  │ProviderChain │  │ToolRegist│ │
│  │ (22 types)│  │  ┌─Provider1 │  │  exec    │ │
│  │           │  │  ├─Provider2 │  │  fs_tools │ │
│  │  pre/post │  │  └─ProviderN │  │  memory   │ │
│  │  hooks    │  │  + cooldowns │  │  mem_srch │ │
│  └───────────┘  │  + schema   │  │  cron     │ │
│                 │    adapter   │  │  subagent │ │
│                 └──────────────┘  │  team_*   │ │
│                                   │  mcp_*    │ │
│  ┌─────────────────────────────┐  └──────────┘ │
│  │ Context Management          │                │
│  │  prune → repair → compact   │                │
│  │  (LLM summary + fallback)   │                │
│  └─────────────────────────────┘                │
│  ┌─────────────────────────────┐                │
│  │ MemorySearchStore           │                │
│  │  SQLite FTS5 + Vector BLOB  │                │
│  │  Hybrid scoring (0.7v+0.3f) │                │
│  └─────────────────────────────┘                │
└─────────────────────────────────────────────────┘
         │
    ┌────┴────┐
    │Channels │  CLI · HTTP · Telegram · Discord · Slack
    └─────────┘
```

- **ProviderChain**: Multi-provider fallback with per-error-type cooldowns. Schema adapter auto-strips unsupported keywords per provider flavor (Gemini/Anthropic/OpenAI).
- **Agent Loop**: system prompt → hook pipeline → tool iterations (max configurable) → LLM compaction when near limit → final reply
- **Tools**: `exec` (allowlisted commands), `read_file`/`write_file`/`edit_file`/`list_dir`/`glob`/`grep_file`/`apply_patch`, `memory`, `memory_search`, `cron`, `subagent`, team tools, MCP tools
- **Channels**: CLI (stdin/stdout), HTTP (/chat, /health), Telegram, stubs for Discord/Slack
- **Cron**: SQLite-backed storage, background polling thread in gateway mode
- **Sessions**: JSONL logs in `~/.minidragon/workspace/sessions/`

## Workspace Structure

```
~/.minidragon/
├── config.json
└── workspace/
    ├── SOUL.md           # Core personality/behavior
    ├── IDENTITY.md       # Agent identity
    ├── USER.md           # User preferences
    ├── AGENTS.md         # Multi-agent instructions
    ├── TOOLS.md          # Tool usage guidance
    ├── MEMORY.md         # Long-term memory
    ├── BOOTSTRAP.md      # First-run onboarding (deleted after use)
    ├── sessions/
    │   └── 2025-01-01.jsonl
    ├── memory/
    │   ├── 2025-01-01.md
    │   └── search.db     # FTS5 + vector search index
    ├── cron/
    │   └── cron.db
    ├── skills/
    │   └── *.json
    └── teams/
        └── {team-name}/
```

## License

MIT
