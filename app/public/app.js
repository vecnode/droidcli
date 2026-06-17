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
  document.querySelectorAll(".panel").forEach((panel) => {
    const active = panel.id === `panel-${name}`;
    panel.classList.toggle("active", active);
    panel.hidden = !active;
  });
}

document.querySelectorAll(".tab").forEach((tab) => {
  tab.addEventListener("click", () => activateTab(tab.dataset.tab));
});

async function refreshStatus() {
  const status = await fetchJson("/api/status");
  document.getElementById("status-host").textContent = status.host || "metaagent_app";
  const sessionParts = [];
  if (status.recording) sessionParts.push("rec");
  if (status.autopilot) sessionParts.push("auto");
  if (status.cinematic) sessionParts.push("cinema");
  document.getElementById("status-session").textContent =
    sessionParts.length ? sessionParts.join(" · ") : "idle";

  document.querySelectorAll(".chip[data-command]").forEach((chip) => {
    const cmd = chip.dataset.command;
    let on = false;
    if (cmd === "toggle_recording") on = !!status.recording;
    if (cmd === "toggle_autopilot") on = !!status.autopilot;
    chip.classList.toggle("active", on);
  });
}

async function refreshConfig() {
  const cfg = await fetchJson("/api/config");
  document.getElementById("cfg-ai").textContent = cfg.ai_enabled ? "on" : "off";
  document.getElementById("cfg-ollama-url").textContent = cfg.ollama_url || "—";
  document.getElementById("cfg-ollama-model").textContent = cfg.ollama_model || "—";
  document.getElementById("cfg-platform-url").textContent = cfg.platform_base_url || "—";
  document.getElementById("cfg-platform-endpoint").textContent = cfg.platform_event_endpoint || "—";
}

async function refreshNotifyLog() {
  const payload = await fetchJson("/api/notify/log");
  const list = document.getElementById("notify-log");
  list.innerHTML = "";
  const entries = payload.entries || [];
  if (!entries.length) {
    const item = document.createElement("li");
    item.textContent = "No messages yet.";
    list.appendChild(item);
    return;
  }
  for (const entry of entries.slice().reverse()) {
    const item = document.createElement("li");
    item.textContent = entry.message || JSON.stringify(entry);
    list.appendChild(item);
  }
}

async function dispatchCommand(command) {
  setHint("command-output", "…");
  const result = await fetchJson("/api/command", {
    method: "POST",
    body: JSON.stringify({ command }),
  });
  if (result.success) {
    setHint("command-output", result.message || "Done.");
  } else {
    setHint("command-output", result.message || "Rejected.", true);
  }
  await refreshStatus();
}

document.querySelectorAll("[data-command]").forEach((button) => {
  button.addEventListener("click", async () => {
    button.disabled = true;
    try {
      await dispatchCommand(button.dataset.command);
    } finally {
      button.disabled = false;
    }
  });
});

document.getElementById("notify-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const input = document.getElementById("notify-input");
  const message = input.value.trim();
  if (!message) return;

  const body = message.startsWith("{") ? message : JSON.stringify({ message });
  await fetch("/notify", { method: "POST", body });
  input.value = "";
  await refreshNotifyLog();
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

refreshStatus();
refreshConfig();
refreshNotifyLog();
setInterval(refreshStatus, 3000);
setInterval(refreshNotifyLog, 4000);
