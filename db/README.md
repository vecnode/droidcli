# db/

droidcli writes all of its persisted runtime data here:

- `droidcli_memory.sqlite3` - the durable agent-turn history (`MemoryStore`,
  see `cli/memory_store.cpp`) behind `GET /api/agent/history` and
  `GET /api/agent/sessions`. Every message in every agent-turn session lives
  here, so conversations survive a restart.
- `droidcli_state.json` - runtime-registered connectors (via
  `POST /api/connectors` or the agent, not `--config`), saved on clean exit
  and reloaded on the next start. Same shape as `--config`'s connectors file;
  see "State persistence" in `AGENTS.md`.
- `droidcli_last_session.json` - which agent-turn session the TUI was last
  on, so it resumes the same conversation instead of starting a new one on
  the next launch (`cli/tui.cpp`).

Created automatically on startup if missing (`DroidHost::initialize()`).
Everything in this directory except this file is git-ignored - it's local
runtime data, not source, and none of it should ever be pushed to GitHub.
