droidcli distribution
======================

Layout
------
  droidcli/       Agent daemon (headless HTTP host). Start via run_all.bat.
  media-player/   openFrameworks media player (media-player-cpp.exe + data/).
  adapter/        LoRA adapter inference code (FastAPI) + the trained adapter
                  weights (~40 MB, included).
  datasets/       Corpus CSVs (*_OCR.csv, *_SUMMARIES.csv, *_OBJS.csv) - for
                  your own tooling; droidcli itself no longer reads these.
  connectors.json Connector config (copy config/connectors.example.json from
                  the repo and edit it, or register connectors at runtime via
                  POST /api/connectors). Points droidcli at media-player/ and
                  adapter/ as ordinary connectors, not built-in peers.
  run_all.bat     Starts droidcli wired to connectors.json.

Requirements on this machine
----------------------------
  - Windows 10/11 x64.
  - For the adapter connector (if you use it): an NVIDIA GPU + driver, and uv
    (https://docs.astral.sh/uv/). First launch creates the local Python env and,
    if no fused model is present at adapter\deploy\merged_model, downloads the
    ~14 GB public base model (llava-hf/llava-1.5-7b-hf) from Hugging Face into
    adapter\training\hf_cache\ - needs internet access once. The trained LoRA
    adapter itself (~40 MB) already ships in this dist, so nothing else to fetch
    for that part. Later launches skip the download (cache check only).
  - For Ollama text-gen (/ai/chat): a local Ollama with at least one model
    installed (--ollama-url / --ollama-model flags, or POST /api/ollama/config).

Quick start
-----------
  1. Copy config/connectors.example.json (from the repo) to connectors.json
     next to run_all.bat and edit base_url/launch_cmd/work_dir as needed.
  2. Run run_all.bat. droidcli listens on http://127.0.0.1:30080.
  3. curl http://127.0.0.1:30080/api/connectors to confirm they loaded.
  4. POST /api/connectors/<id>/launch (launched_process) or
     /api/connectors/<id>/call (http_peer) to drive a connector directly, or
     POST /api/tasks to queue work for droidcli to dispatch itself.

All URLs, dirs, and commands can also be changed at runtime via
POST /api/config, POST /api/ollama/config, and the /api/connectors routes.
