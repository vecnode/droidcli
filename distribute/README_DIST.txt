droidcli distribution
======================

Layout
------
  droidcli.exe          Agent daemon. No window - launches an FTXUI terminal
                         dashboard by default, or pass --headless for scripted
                         use. Start via run_all.bat.
  *.dll                  FFmpeg runtime DLLs (media decode).
  config/
    connectors.example.json   Template - copy to connectors.json and edit.
  connectors.json        Your connector config (not shipped - copy the
                          example above and edit base_url/launch_cmd/work_dir
                          for whatever peer apps you want droidcli to control).
  run_all.bat             Starts droidcli wired to connectors.json.

droidcli has no compiled-in knowledge of any peer app. Connectors are either
an http_peer (reached by URL) or a launched_process (a local command droidcli
can start/stop and track by PID) - entirely your own configuration.

Requirements on this machine
----------------------------
  - Windows 10/11 x64.
  - For Ollama text-gen (/ai/chat): a local Ollama with at least one model
    installed (--ollama-url / --ollama-model flags, or POST /api/ollama/config).

Quick start
-----------
  1. Copy config/connectors.example.json to connectors.json next to
     run_all.bat and edit it to point at your own peer apps.
  2. Run run_all.bat. droidcli listens on http://127.0.0.1:30080 and opens
     the terminal dashboard.
  3. curl http://127.0.0.1:30080/api/connectors to confirm they loaded.
  4. POST /api/connectors/<id>/launch (launched_process) or
     /api/connectors/<id>/call (http_peer) to drive a connector directly, or
     POST /api/tasks to queue work for droidcli to dispatch itself.

All URLs and connectors can also be changed at runtime via POST /api/config,
POST /api/ollama/config, and the /api/connectors routes.
