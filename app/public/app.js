async function fetchJson(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  const text = await response.text();
  try {
    return JSON.parse(text);
  } catch {
    return { raw: text, ok: response.ok, status: response.status };
  }
}

function setHint(id, message, isError = false) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = message;
  el.classList.toggle("error", isError);
}

function makeEl(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text != null) node.textContent = text;
  return node;
}

function activateTab(name) {
  document.querySelectorAll(".tab").forEach((tab) => {
    const active = tab.dataset.tab === name;
    tab.classList.toggle("active", active);
    tab.setAttribute("aria-selected", active ? "true" : "false");
  });
  document.querySelectorAll(".panel-view").forEach((panel) => {
    const active = panel.id === `panel-${name}`;
    panel.classList.toggle("active", active);
    panel.hidden = !active;
  });
}

document.querySelectorAll(".tab").forEach((tab) => {
  tab.addEventListener("click", () => activateTab(tab.dataset.tab));
});

function renderRuntimeRow(runtime) {
  const row = document.createElement("li");
  row.className = "runtime-row";
  const running = !!runtime.active_in_session;
  row.innerHTML = `
    <span class="status-lamp ${running ? "on" : "off"}" title="${running ? "Running" : "Stopped"}"></span>
    <span class="runtime-row-name">${runtime.title}</span>
  `;
  return row;
}

function appendRuntimeGroup(container, title, runtimes) {
  if (!runtimes.length) return;

  const section = document.createElement("div");
  section.className = "runtime-section";
  section.innerHTML = `<h3 class="runtime-section-title">${title}</h3>`;

  const list = document.createElement("ul");
  list.className = "runtime-rows";
  for (const runtime of runtimes) {
    list.appendChild(renderRuntimeRow(runtime));
  }
  section.appendChild(list);
  container.appendChild(section);
}

function formatMediaStatus(status) {
  if (!status || !status.loaded) {
    return "no clip loaded";
  }

  const transport = status.playing ? "playing" : "stopped";
  return `${(status.clipIndex ?? 0) + 1}/${status.clipCount ?? 0}  ${status.clipName || "—"}  (${transport})`;
}

async function refreshNetwork() {
  const network = await fetchJson("/api/network/status");
  const online = !!network.media_player_online;

  const mediaLamp = document.getElementById("media-lamp");
  if (mediaLamp) {
    mediaLamp.classList.toggle("on", online);
    mediaLamp.classList.toggle("off", !online);
    mediaLamp.title = online ? "Media player online" : "Media player offline";
  }

  const url = network.media_player_url || "media player";

  const detectLabel = document.getElementById("media-detection-label");
  if (detectLabel) {
    detectLabel.textContent = online
      ? `Detection: media-player-cpp online (${url})`
      : `Detection: media-player-cpp offline (${url})`;
    detectLabel.classList.toggle("error", !online);
  }
}

async function refreshMediaStatus() {
  const result = await fetchJson("/api/media/status");
  const label = document.getElementById("media-status-label");
  const subtitles = document.getElementById("media-subtitles");

  if (result.ok === false || result.error) {
    if (label) {
      label.textContent = result.error || "Media player unreachable";
      label.classList.add("error");
    }
    return;
  }

  const status = result.status || result;
  if (label) {
    label.textContent = formatMediaStatus(status);
    label.classList.remove("error");
  }

  if (subtitles && typeof status.subtitlesEnabled === "boolean") {
    subtitles.checked = status.subtitlesEnabled;
  }
}

async function mediaCommand(path, body = null) {
  const options = { method: "POST" };
  if (body !== null) {
    options.body = JSON.stringify(body);
  }

  const result = await fetchJson(path, options);
  if (result.ok === false || result.error) {
    const label = document.getElementById("media-status-label");
    if (label) {
      label.textContent = result.error || "Media player command failed";
      label.classList.add("error");
    }
    await refreshCommsLog();
    return result;
  }

  await refreshMediaStatus();
  await refreshCommsLog();
  return result;
}

async function refreshMediaClips() {
  const select = document.getElementById("media-clip-select");
  if (!select) return;

  const result = await fetchJson("/api/media/clips");
  select.innerHTML = "";

  const clips = Array.isArray(result) ? result : result.clips || [];
  if (!clips.length) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = "No clips available";
    select.appendChild(option);
    return;
  }

  const placeholder = document.createElement("option");
  placeholder.value = "";
  placeholder.textContent = "Select clip index…";
  select.appendChild(placeholder);

  for (const clip of clips) {
    const option = document.createElement("option");
    option.value = String(clip.index);
    option.textContent = `[${clip.index}] ${clip.name || clip.path || "clip"}`;
    select.appendChild(option);
  }
}

async function refreshRuntimes() {
  const container = document.getElementById("runtime-sections");
  if (!container) return;

  const payload = await fetchJson("/api/runtimes");
  const runtimes = payload.runtimes || [];
  container.innerHTML = "";

  appendRuntimeGroup(container, "Core", runtimes);

  if (!runtimes.length) {
    container.innerHTML = "<p class='muted'>No runtimes reported.</p>";
  }
}

let lastConfig = {};

function applyConfigToForm(config) {
  lastConfig = config || {};
  const fields = {
    "cfg-ollama-url": lastConfig.ollama_url,
    "cfg-ollama-model": lastConfig.ollama_model,
    "cfg-media-url": lastConfig.media_player_base_url,
    "cfg-adapter-url": lastConfig.adapter_url,
    "cfg-media-dir": lastConfig.media_player_project_dir,
    "cfg-media-build": lastConfig.media_player_build_command,
    "cfg-media-run": lastConfig.media_player_run_command,
    "cfg-adapter-dir": lastConfig.adapter_project_dir,
    "cfg-adapter-launch": lastConfig.adapter_launch_command,
    "cfg-dataset-dir": lastConfig.dataset_output_dir,
  };
  for (const [id, value] of Object.entries(fields)) {
    const input = document.getElementById(id);
    // Don't clobber a field the user is actively editing.
    if (input && document.activeElement !== input) {
      if (input.tagName === "SELECT" && value && ![...input.options].some((o) => o.value === value)) {
        input.appendChild(new Option(value, value));
      }
      input.value = value || "";
    }
  }
}

async function refreshConfig() {
  const config = await fetchJson("/api/config");
  applyConfigToForm(config);
}

async function refreshDataset() {
  const list = document.getElementById("corpus-list");
  if (!list) return;

  const data = await fetchJson("/api/dataset");
  const count = document.getElementById("corpus-count");
  const status = document.getElementById("corpus-status");
  list.innerHTML = "";

  if (!data.ok) {
    if (count) count.textContent = "";
    if (status) status.textContent = data.error || "No dataset configured.";
    return;
  }

  if (count) count.textContent = `· ${data.count}`;
  if (status) {
    status.textContent = `${data.shown} of ${data.count} pages · ${data.dataset_dir}`;
  }

  for (const entry of data.entries || []) {
    const item = makeEl("li", "corpus-item");
    const head = makeEl("div", "corpus-item-head");
    head.appendChild(makeEl("span", "corpus-name", entry.image));
    const meta = (entry.status || "") + (entry.objects ? ` · ${entry.objects} obj` : "");
    head.appendChild(makeEl("span", "corpus-meta", meta));
    item.appendChild(head);
    const preview = entry.summary_preview || entry.ocr_preview || "";
    if (preview) item.appendChild(makeEl("div", "corpus-preview", preview));
    list.appendChild(item);
  }
}

async function refreshAdapter() {
  const status = await fetchJson("/api/adapter/status");
  const lamp = document.getElementById("adapter-lamp");
  const hint = document.getElementById("adapter-status-hint");
  const online = !!status.online;

  if (lamp) {
    lamp.className = `adapter-lamp status-lamp ${online ? "on" : "off"}`;
    lamp.title = online ? "Adapter online" : "Adapter offline";
  }
  if (hint) {
    if (online) {
      const bits = [status.mode, status.dtype, status.device].filter(Boolean).join(" · ");
      hint.textContent = bits ? `online — ${bits}` : "online";
      hint.classList.remove("error");
    } else {
      hint.textContent = `offline (${status.adapter_url || "adapter"})`;
      hint.classList.add("error");
    }
  }
}

function procText(proc) {
  if (!proc) return "idle";
  const pid = proc.pid ? ` PID ${proc.pid}` : "";
  return `${proc.status || (proc.running ? "running" : "idle")}${pid}`;
}

async function refreshProcessStatus() {
  const status = await fetchJson("/api/process/status");
  const processes = status.processes || [];
  const get = (key) => processes.find((proc) => proc.key === key);

  const mediaLabel = document.getElementById("media-process-status");
  if (mediaLabel) {
    const build = get("media_build");
    const run = get("media_run");
    const parts = [];
    if (build) parts.push(`build: ${procText(build)}`);
    if (run) parts.push(`run: ${procText(run)}`);
    mediaLabel.textContent = "Process: " + (parts.length ? parts.join("  ·  ") : "idle");
  }

  const adapterLabel = document.getElementById("adapter-process-status");
  if (adapterLabel) {
    adapterLabel.textContent = "Process: " + procText(get("adapter_server"));
  }
}

async function postProcess(path) {
  await fetchJson(path, { method: "POST" });
  await refreshProcessStatus();
  await refreshCommsLog();
}

function fillModelSelect(selectId, models, selected) {
  const select = document.getElementById(selectId);
  if (!select || document.activeElement === select) return;

  const previous = select.value;
  const names = models && models.length ? models.slice() : [];
  if (selected && !names.includes(selected)) names.unshift(selected);

  select.innerHTML = "";
  if (!names.length) {
    select.appendChild(new Option(selected || "Refresh to load models…", selected || ""));
  } else {
    for (const name of names) select.appendChild(new Option(name, name));
  }

  if (previous && names.includes(previous)) select.value = previous;
  else if (selected) select.value = selected;
}

async function refreshOllama() {
  const status = await fetchJson("/api/ollama/status");
  const lamp = document.getElementById("ollama-lamp");
  const text = document.getElementById("ollama-status-text");
  const urlHint = document.getElementById("ollama-url-hint");

  if (!status.ai_enabled) {
    lamp.className = "status-lamp off";
    text.textContent = "AI disabled (METAAGENT_NO_AI)";
    urlHint.textContent = status.ollama_url ? `URL: ${status.ollama_url}` : "";
    return;
  }

  const online = !!status.online;
  lamp.className = `status-lamp ${online ? "on" : "off"}`;
  text.textContent = online ? "Ollama is running" : "Ollama is not reachable";
  urlHint.textContent = `URL: ${status.ollama_url || "—"}`;

  const modelHint = document.getElementById("chat-model-hint");
  if (modelHint) {
    modelHint.textContent = status.model || "model";
  }

  const models = status.models || [];
  const selected = status.model || "";
  // The Ollama panel select and the Settings → Endpoints model dropdown.
  fillModelSelect("ollama-model", models, selected);
  fillModelSelect("cfg-ollama-model", models, selected);
}

async function refreshCommsLog() {
  const mediaLog = await fetchJson("/api/app/log");
  const entries = (mediaLog.entries || []).map((entry) => ({
    text: `${entry.timestamp || "--:--:--"} [${(entry.channel || "app").toUpperCase()}] [${(entry.direction || "event").toUpperCase()}] ${entry.summary}`,
    direction: entry.direction || "event",
    delivered: !!entry.success,
  }));

  const list = document.getElementById("comms-log");
  list.innerHTML = "";
  if (!entries.length) {
    list.innerHTML = "<li class='log-item muted'>No media commands yet.</li>";
    return;
  }

  for (const entry of entries.slice().reverse()) {
    const item = document.createElement("li");
    item.className = `log-item log-${entry.direction} ${entry.delivered ? "ok" : "fail"}`;
    item.textContent = entry.text;
    list.appendChild(item);
  }
}

document.getElementById("media-next").addEventListener("click", () => mediaCommand("/api/media/next"));
document.getElementById("media-previous").addEventListener("click", () => mediaCommand("/api/media/previous"));
document.getElementById("media-refresh-clips").addEventListener("click", async () => {
  await refreshMediaClips();
  await refreshCommsLog();
});

document.getElementById("media-open-clip").addEventListener("click", async () => {
  const select = document.getElementById("media-clip-select");
  if (!select || select.value === "") return;
  await mediaCommand(`/api/media/clips/${select.value}`);
});

document.getElementById("media-subtitles").addEventListener("change", (event) => {
  mediaCommand("/api/media/subtitles", { enabled: event.target.checked });
});

// Agent loop: when started, trigger the media player "Next" every 20 seconds
// (same action as the Next button). Further per-tick logic can be added here.
const kAgentIntervalMs = 20000;
let agentTimer = null;

function agentTick() {
  mediaCommand("/api/media/next");
}

function setAgentRunning(running) {
  const toggle = document.getElementById("agent-toggle");
  const status = document.getElementById("agent-status");
  toggle.textContent = running ? "Stop" : "Start";
  toggle.classList.toggle("active", running);
  status.textContent = running ? "Running — Next every 20s" : "Stopped";
}

document.getElementById("agent-toggle").addEventListener("click", () => {
  if (agentTimer) {
    clearInterval(agentTimer);
    agentTimer = null;
    setAgentRunning(false);
    return;
  }
  agentTick();
  agentTimer = setInterval(agentTick, kAgentIntervalMs);
  setAgentRunning(true);
});

document.getElementById("chat-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const input = document.getElementById("chat-input");
  const prompt = input.value.trim();
  if (!prompt) return;

  const output = document.getElementById("chat-output");
  output.textContent = "Thinking…";

  const result = await fetchJson("/ai/chat", {
    method: "POST",
    body: JSON.stringify({ prompt }),
  });

  output.textContent =
    result.assistant || result.message || result.error || JSON.stringify(result, null, 2);
});

document.getElementById("adapter-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const input = document.getElementById("adapter-input");
  const ocrText = input.value.trim();
  if (!ocrText) return;

  const output = document.getElementById("adapter-output");
  output.textContent = "Generating…";

  const result = await fetchJson("/api/adapter/summarize", {
    method: "POST",
    body: JSON.stringify({ ocr_text: ocrText }),
  });

  if (result.summary) {
    const meta = result.elapsed_ms ? `\n\n— ${result.elapsed_ms} ms` : "";
    output.textContent = result.summary + meta;
  } else {
    output.textContent = result.error || JSON.stringify(result, null, 2);
  }
  await refreshCommsLog();
});

document.getElementById("media-build").addEventListener("click", () => postProcess("/api/media/build"));
document.getElementById("media-run").addEventListener("click", () => postProcess("/api/media/run"));
document.getElementById("media-process-stop").addEventListener("click", () => postProcess("/api/media/process/stop"));
document.getElementById("adapter-launch").addEventListener("click", () => postProcess("/api/adapter/launch"));
document.getElementById("adapter-process-stop").addEventListener("click", () => postProcess("/api/adapter/process/stop"));
document.getElementById("corpus-refresh").addEventListener("click", refreshDataset);

document.getElementById("ollama-refresh").addEventListener("click", refreshOllama);

document.getElementById("endpoints-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const payload = {
    ollama_url: document.getElementById("cfg-ollama-url").value.trim(),
    ollama_model: document.getElementById("cfg-ollama-model").value.trim(),
    media_player_base_url: document.getElementById("cfg-media-url").value.trim(),
    adapter_url: document.getElementById("cfg-adapter-url").value.trim(),
    media_player_project_dir: document.getElementById("cfg-media-dir").value.trim(),
    media_player_build_command: document.getElementById("cfg-media-build").value.trim(),
    media_player_run_command: document.getElementById("cfg-media-run").value.trim(),
    adapter_project_dir: document.getElementById("cfg-adapter-dir").value.trim(),
    adapter_launch_command: document.getElementById("cfg-adapter-launch").value.trim(),
    dataset_output_dir: document.getElementById("cfg-dataset-dir").value.trim(),
  };

  const result = await fetchJson("/api/config", {
    method: "POST",
    body: JSON.stringify(payload),
  });

  if (result.success) {
    applyConfigToForm(result);
    setHint("endpoints-output", "Endpoints applied.", false);
    await Promise.all([refreshNetwork(), refreshOllama(), refreshAdapter(), refreshDataset()]);
    // New dataset dir → refresh the subtitle for the current clip.
    await syncSubtitle();
  } else {
    setHint("endpoints-output", result.message || "Update failed.", true);
  }
});

document.getElementById("endpoints-reset").addEventListener("click", () => {
  applyConfigToForm(lastConfig);
  setHint("endpoints-output", "", false);
});

document.getElementById("ollama-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const model = document.getElementById("ollama-model").value.trim();
  if (!model) return;

  const result = await fetchJson("/api/ollama/config", {
    method: "POST",
    body: JSON.stringify({ model }),
  });

  setHint(
    "settings-output",
    result.success ? `Model set to ${result.model}.` : result.message || "Update failed.",
    !result.success
  );
  await refreshOllama();
});

async function refreshAll() {
  await Promise.all([
    refreshNetwork(),
    refreshMediaStatus(),
    refreshMediaClips(),
    refreshRuntimes(),
    refreshOllama(),
    refreshConfig(),
    refreshAdapter(),
    refreshProcessStatus(),
    refreshDataset(),
    refreshCommsLog(),
  ]);
}

async function syncSubtitle() {
  await fetchJson("/api/media/subtitle-sync", { method: "POST" });
}

refreshAll();
// Push the true summary for whatever clip is showing on startup.
syncSubtitle();
setInterval(refreshNetwork, 5000);
setInterval(refreshMediaStatus, 5000);
setInterval(refreshCommsLog, 2500);
setInterval(refreshMediaClips, 10000);
setInterval(refreshRuntimes, 5000);
setInterval(refreshOllama, 15000);
setInterval(refreshAdapter, 10000);
setInterval(refreshProcessStatus, 4000);
