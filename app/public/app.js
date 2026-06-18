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

function isUe5Runtime(runtime) {
  return runtime.host_scope === "ue5";
}

function runtimeTitle(runtime) {
  return isUe5Runtime(runtime) ? `UE5 ${runtime.title}` : runtime.title;
}

function renderRuntimeRow(runtime) {
  const row = document.createElement("li");
  row.className = "runtime-row";
  const running = !!runtime.active_in_session;
  row.innerHTML = `
    <span class="status-lamp ${running ? "on" : "off"}" title="${running ? "Running" : "Stopped"}"></span>
    <span class="runtime-row-name">${runtimeTitle(runtime)}</span>
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
  document.getElementById("network-dot").classList.toggle("online", online);
  document.getElementById("network-dot").classList.toggle("offline", !online);

  const url = network.media_player_url || "media player";
  document.getElementById("network-label").textContent = online
    ? `Media Player online`
    : `Media Player offline`;
  document.getElementById("network-badge").title = url;
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

async function refreshRuntimes() {
  const container = document.getElementById("runtime-sections");
  if (!container) return;

  const payload = await fetchJson("/api/runtimes");
  const runtimes = payload.runtimes || [];
  const ue5Enabled = !!payload.ue5_runtimes_enabled;
  container.innerHTML = "";

  const toggle = document.getElementById("ue5-runtimes-toggle");
  const hint = document.getElementById("ue5-runtimes-hint");
  if (toggle) {
    toggle.classList.toggle("active", ue5Enabled);
    toggle.textContent = ue5Enabled ? "Disable UE5 Runtimes" : "Enable UE5 Runtimes";
  }
  if (hint) {
    hint.textContent = ue5Enabled
      ? "UE5 plugin runtimes are enabled."
      : "UE5 plugin runtimes are off by default.";
  }

  const core = runtimes.filter((runtime) => !isUe5Runtime(runtime));
  const ue5 = runtimes.filter((runtime) => isUe5Runtime(runtime));

  appendRuntimeGroup(container, "Core", core);

  if (core.length && ue5.length) {
    const divider = document.createElement("div");
    divider.className = "runtime-divider";
    divider.setAttribute("role", "separator");
    divider.textContent = "Unreal Engine 5";
    container.appendChild(divider);
  }

  appendRuntimeGroup(container, "UE5 Plugin", ue5);

  if (!core.length && !ue5.length) {
    container.innerHTML = "<p class='muted'>No runtimes reported.</p>";
  }
}

async function refreshOllama() {
  const status = await fetchJson("/api/ollama/status");
  const lamp = document.getElementById("ollama-lamp");
  const text = document.getElementById("ollama-status-text");
  const modelSelect = document.getElementById("ollama-model");
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

  if (!modelSelect) return;

  const models = status.models || [];
  const selected = status.model || "";
  const previous = modelSelect.value;
  modelSelect.innerHTML = "";

  if (!models.length) {
    const option = document.createElement("option");
    option.value = selected;
    option.textContent = selected || "No models found";
    modelSelect.appendChild(option);
  } else {
    for (const name of models) {
      const option = document.createElement("option");
      option.value = name;
      option.textContent = name;
      if (name === selected) {
        option.selected = true;
      }
      modelSelect.appendChild(option);
    }
  }

  if (previous && [...modelSelect.options].some((option) => option.value === previous)) {
    modelSelect.value = previous;
  } else if (selected) {
    modelSelect.value = selected;
  }
}

async function refreshCommsLog() {
  const mediaLog = await fetchJson("/api/media/log");
  const entries = (mediaLog.entries || []).map((entry) => ({
    text: `[${entry.action}] ${entry.summary}`,
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
    item.className = `log-item ${entry.delivered ? "ok" : "fail"}`;
    item.textContent = entry.text;
    list.appendChild(item);
  }
}

document.getElementById("media-play").addEventListener("click", () => mediaCommand("/api/media/play"));
document.getElementById("media-next").addEventListener("click", () => mediaCommand("/api/media/next"));
document.getElementById("media-stop").addEventListener("click", () => mediaCommand("/api/media/stop"));
document.getElementById("media-subtitles").addEventListener("change", (event) => {
  mediaCommand("/api/media/subtitles", { enabled: event.target.checked });
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

document.getElementById("ollama-refresh").addEventListener("click", refreshOllama);

document.getElementById("ue5-runtimes-toggle").addEventListener("click", async () => {
  const payload = await fetchJson("/api/runtimes");
  const nextEnabled = !payload.ue5_runtimes_enabled;
  const result = await fetchJson("/api/runtimes/ue5", {
    method: "POST",
    body: JSON.stringify({ enabled: nextEnabled }),
  });
  if (!result.success) {
    setHint("settings-output", result.message || "Failed to update UE5 runtimes.", true);
    return;
  }
  await refreshRuntimes();
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
    refreshRuntimes(),
    refreshOllama(),
    refreshCommsLog(),
  ]);
}

refreshAll();
setInterval(refreshNetwork, 3000);
setInterval(refreshMediaStatus, 2500);
setInterval(refreshCommsLog, 2500);
setInterval(refreshRuntimes, 5000);
setInterval(refreshOllama, 15000);
