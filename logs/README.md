# logs/

droidcli writes a persistent log of every session here (`log.jsonl`) - the
same events already printed to the console via `DroidHost::append_app_log`,
but durable across restarts and crashes, so you can see what happened even
after the process is gone.

**Structured JSONL, not plain text**: one JSON object per line -
`{"ts":"...","channel":"...","direction":"...","summary":"...","success":bool}`,
plus `"session_id"` on `"chat"`-channel entries (correlating a log line with
an agent-turn session, see `MemoryStore` / `GET /api/agent/history` in
`ARCHITECTURE.md`). The console output stays human-readable bracketed text -
this file is for scripts/tools to parse, not for reading directly. Each
session's start is also a JSON line: `{"event":"session_started","ts":"..."}`.

Created automatically on startup if missing. Everything in this directory
except this file is git-ignored - log contents are local runtime data, not
source.
