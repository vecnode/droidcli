MetaAgent distribution
======================

Layout
------
  metaagent/      Agent controller (WebView UI + HTTP host). Start via run_all.bat.
  media-player/   openFrameworks media player (media-player-cpp.exe + data/).
  adapter/        LoRA adapter inference code (FastAPI) + the trained adapter
                  weights (~40 MB, included). Launched from metaagent (Document
                  Adapter -> Launch server) or adapter\deploy\deploy.bat.
  datasets/       Corpus CSVs (*_OCR.csv, *_SUMMARIES.csv, *_OBJS.csv).
  run_all.bat     Starts metaagent wired to the folders above.

Requirements on this machine
----------------------------
  - Windows 10/11 x64 with the WebView2 Runtime (usually preinstalled).
  - For the adapter: an NVIDIA GPU + driver, and uv
    (https://docs.astral.sh/uv/). First launch creates the local Python env and,
    if no fused model is present at adapter\deploy\merged_model, downloads the
    ~14 GB public base model (llava-hf/llava-1.5-7b-hf) from Hugging Face into
    adapter\training\hf_cache\ - needs internet access once. The trained LoRA
    adapter itself (~40 MB) already ships in this dist, so nothing else to fetch
    for that part. Later launches skip the download (cache check only).
  - For AI subtitles / the Agent panel: a local Ollama with at least one model
    installed (pick it in metaagent Settings -> Endpoints).

Quick start
-----------
  1. Run run_all.bat.
  2. In metaagent: Media Player -> RunRelease to start the player.
  3. Document Adapter -> Launch server to start the adapter (first run is slow).
  4. Agent -> Start to begin the control loop.

All URLs, dirs, and commands can be changed in Settings -> Endpoints.
