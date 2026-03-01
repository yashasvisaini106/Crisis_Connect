const config = window.CRISISCONNECT_CONFIG || {};
const apiBase = String(config.apiBase || "").replace(/\/+$/, "");
const wsBase = String(config.wsBase || "").replace(/\/+$/, "");

function inferWsBase() {
    if (wsBase) {
        return wsBase;
    }

    if (apiBase) {
        return apiBase.replace(/^http:/i, "ws:").replace(/^https:/i, "wss:");
    }

    const wsProtocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    return `${wsProtocol}//${window.location.host}`;
}

const loginView = document.getElementById("loginView");
const dashboardView = document.getElementById("dashboardView");
const loginBtn = document.getElementById("loginBtn");
const logoutBtn = document.getElementById("logoutBtn");
const loginName = document.getElementById("loginName");
const loginRole = document.getElementById("loginRole");
const loginError = document.getElementById("loginError");
const welcomeText = document.getElementById("welcomeText");
const tcpStatus = document.getElementById("tcpStatus");

const victimPanel = document.getElementById("victimPanel");
const responderPanel = document.getElementById("responderPanel");
const adminPanel = document.getElementById("adminPanel");

const victimType = document.getElementById("victimType");
const victimLocation = document.getElementById("victimLocation");
const victimMessage = document.getElementById("victimMessage");
const sendSosBtn = document.getElementById("sendSosBtn");
const sendAlertBtn = document.getElementById("sendAlertBtn");

const refreshResponderBtn = document.getElementById("refreshResponderBtn");
const assignCaseId = document.getElementById("assignCaseId");
const assignResponder = document.getElementById("assignResponder");
const assignBtn = document.getElementById("assignBtn");
const resolveBtn = document.getElementById("resolveBtn");

const caseFilter = document.getElementById("caseFilter");
const caseSearch = document.getElementById("caseSearch");
const caseList = document.getElementById("caseList");

const chatCaseId = document.getElementById("chatCaseId");
const chatInput = document.getElementById("chatInput");
const chatSendBtn = document.getElementById("chatSendBtn");
const chatFeed = document.getElementById("chatFeed");

const mTotal = document.getElementById("mTotal");
const mActive = document.getElementById("mActive");
const mResolved = document.getElementById("mResolved");
const mHigh = document.getElementById("mHigh");

const state = {
    token: "",
    user: null,
    ws: null,
    emergencies: [],
    chatLog: []
};

function escapeHtml(text) {
    return String(text)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/\"/g, "&quot;")
        .replace(/'/g, "&#39;");
}

async function api(path, options = {}) {
    const headers = { "Content-Type": "application/json", ...(options.headers || {}) };
    if (state.token) headers.Authorization = `Bearer ${state.token}`;

    const response = await fetch(`${apiBase}${path}`, {
        ...options,
        headers
    });

    const data = await response.json().catch(() => ({}));
    if (!response.ok) {
        throw new Error(data.error || "Request failed");
    }
    return data;
}

function setRolePanels(role) {
    victimPanel.classList.toggle("hidden", role !== "CITIZEN");
    responderPanel.classList.toggle("hidden", role !== "RESPONDER");
    adminPanel.classList.toggle("hidden", role !== "ADMIN");
}

function updateMetrics() {
    const total = state.emergencies.length;
    const active = state.emergencies.filter((e) => e.status === "ACTIVE").length;
    const resolved = state.emergencies.filter((e) => e.status === "RESOLVED").length;
    const high = state.emergencies.filter((e) => e.priority === "HIGH").length;

    mTotal.textContent = String(total);
    mActive.textContent = String(active);
    mResolved.textContent = String(resolved);
    mHigh.textContent = String(high);
}

function filteredCases() {
    const filter = caseFilter.value;
    const q = caseSearch.value.trim().toLowerCase();

    return state.emergencies.filter((e) => {
        if (filter !== "ALL" && e.status !== filter) return false;

        if (!q) return true;
        const hay = `${e.id} ${e.type} ${e.location} ${e.victim} ${e.message}`.toLowerCase();
        return hay.includes(q);
    });
}

function renderCases() {
    const rows = filteredCases();
    if (!rows.length) {
        caseList.innerHTML = '<div class="case-item">No cases available.</div>';
        updateMetrics();
        return;
    }

    caseList.innerHTML = rows.map((e) => `
        <article class="case-item ${e.priority === "HIGH" ? "high" : ""}">
            <div class="case-head">
                <span>Case #${e.id} | ${escapeHtml(e.type)} | ${escapeHtml(e.status)}</span>
                <span>${new Date(e.createdAt).toLocaleString()}</span>
            </div>
            <div><strong>Victim:</strong> ${escapeHtml(e.victim)}</div>
            <div><strong>Location:</strong> ${escapeHtml(e.location)}</div>
            <div><strong>Assigned:</strong> ${escapeHtml(e.assignedResponder || "Unassigned")}</div>
            <div>${escapeHtml(e.message)}</div>
        </article>
    `).join("");

    updateMetrics();
}

function pushChatLine(line) {
    state.chatLog.push(line);
    if (state.chatLog.length > 250) state.chatLog.shift();

    chatFeed.innerHTML = state.chatLog.map((c) => `
        <article class="chat-line ${c.priority === "HIGH" ? "high" : ""}">
            <div class="chat-meta">${escapeHtml(c.meta)}</div>
            <div>${escapeHtml(c.text)}</div>
        </article>
    `).join("");

    chatFeed.scrollTop = chatFeed.scrollHeight;
}

function mergeEmergency(updated) {
    const idx = state.emergencies.findIndex((e) => e.id === updated.id);
    if (idx === -1) {
        state.emergencies.unshift(updated);
    } else {
        state.emergencies[idx] = updated;
    }
    renderCases();
}

function connectWs() {
    if (state.ws) {
        state.ws.close();
    }

    const ws = new WebSocket(inferWsBase());
    state.ws = ws;

    ws.addEventListener("open", () => {
        tcpStatus.textContent = "TCP Bridge: Connecting...";
        ws.send(JSON.stringify({ action: "auth", token: state.token }));
    });

    ws.addEventListener("message", (event) => {
        let data;
        try {
            data = JSON.parse(event.data.toString());
        } catch {
            return;
        }

        if (data.type === "auth_ok") {
            tcpStatus.textContent = "TCP Bridge: Connected";
            return;
        }

        if (data.type === "auth_error") {
            tcpStatus.textContent = "TCP Bridge: Auth failed";
            return;
        }

        if (data.type === "tcp_status") {
            tcpStatus.textContent = `TCP Bridge: ${data.connected ? "Connected" : "Disconnected"}`;
            return;
        }

        if (data.type === "snapshot") {
            state.emergencies = data.emergencies || [];
            renderCases();
            return;
        }

        if (data.type === "emergency_created" || data.type === "emergency_updated") {
            mergeEmergency(data.emergency);
            return;
        }

        if (data.type === "case_chat") {
            pushChatLine({
                priority: "NORMAL",
                meta: `Case #${data.caseId} | ${data.chat.sender} (${data.chat.role}) | ${new Date(data.chat.timestamp).toLocaleString()}`,
                text: data.chat.text
            });
            return;
        }

        if (data.type === "system") {
            pushChatLine({
                priority: data.level === "warn" ? "HIGH" : "NORMAL",
                meta: `System | ${new Date().toLocaleString()}`,
                text: data.text
            });
            return;
        }

        if (data.type === "tcp_event") {
            const payload = data.payload || {};
            if (payload.type === "chat") {
                pushChatLine({
                    priority: payload.priority || "NORMAL",
                    meta: `${payload.sender} (${payload.role}) @ ${payload.location} | ${payload.timestamp}`,
                    text: payload.text
                });
            }
        }
    });

    ws.addEventListener("close", () => {
        tcpStatus.textContent = "TCP Bridge: Disconnected";
    });
}

async function bootstrap() {
    const data = await api("/api/bootstrap");
    state.user = data.user;
    state.emergencies = data.emergencies || [];
    welcomeText.textContent = `${state.user.username} (${state.user.role})`;
    setRolePanels(state.user.role);
    renderCases();
    connectWs();
}

loginBtn.addEventListener("click", async () => {
    loginError.textContent = "";
    const username = loginName.value.trim();
    const role = loginRole.value;

    if (!username) {
        loginError.textContent = "Username is required.";
        return;
    }

    try {
        const result = await api("/api/login", {
            method: "POST",
            body: JSON.stringify({ username, role })
        });

        state.token = result.token;
        loginView.classList.add("hidden");
        dashboardView.classList.remove("hidden");
        await bootstrap();
    } catch (err) {
        loginError.textContent = err.message;
    }
});

logoutBtn.addEventListener("click", () => {
    state.token = "";
    state.user = null;
    state.emergencies = [];
    state.chatLog = [];
    if (state.ws) {
        state.ws.close();
        state.ws = null;
    }

    dashboardView.classList.add("hidden");
    loginView.classList.remove("hidden");
    renderCases();
    pushChatLine({ priority: "NORMAL", meta: "System", text: "Logged out." });
});

async function createEmergency(sos) {
    try {
        const payload = {
            sos,
            type: victimType.value,
            location: victimLocation.value.trim(),
            message: victimMessage.value.trim(),
            timestamp: new Date().toISOString()
        };

        if (!payload.message) {
            alert("Please add emergency message.");
            return;
        }

        await api("/api/emergencies", {
            method: "POST",
            body: JSON.stringify(payload)
        });

        victimMessage.value = "";
        pushChatLine({
            priority: "HIGH",
            meta: `Emergency sent | ${new Date().toLocaleString()}`,
            text: `${payload.type} alert submitted at ${payload.location || "Unknown location"}`
        });
    } catch (err) {
        alert(err.message);
    }
}

sendSosBtn.addEventListener("click", () => createEmergency(true));
sendAlertBtn.addEventListener("click", () => createEmergency(false));

refreshResponderBtn.addEventListener("click", async () => {
    try {
        const data = await api("/api/emergencies");
        state.emergencies = data.emergencies || [];
        renderCases();
    } catch (err) {
        alert(err.message);
    }
});

assignBtn.addEventListener("click", async () => {
    const id = Number(assignCaseId.value);
    const responder = assignResponder.value.trim();
    if (!id || !responder) {
        alert("Provide case id and responder username.");
        return;
    }

    try {
        await api(`/api/emergencies/${id}/assign`, {
            method: "POST",
            body: JSON.stringify({ responder })
        });
    } catch (err) {
        alert(err.message);
    }
});

resolveBtn.addEventListener("click", async () => {
    const id = Number(assignCaseId.value);
    if (!id) {
        alert("Provide case id.");
        return;
    }

    try {
        await api(`/api/emergencies/${id}/resolve`, { method: "POST" });
    } catch (err) {
        alert(err.message);
    }
});

chatSendBtn.addEventListener("click", async () => {
    const id = Number(chatCaseId.value);
    const text = chatInput.value.trim();
    if (!id || !text) {
        alert("Provide case id and message.");
        return;
    }

    try {
        await api(`/api/emergencies/${id}/chat`, {
            method: "POST",
            body: JSON.stringify({ text })
        });
        chatInput.value = "";
    } catch (err) {
        alert(err.message);
    }
});

caseFilter.addEventListener("change", renderCases);
caseSearch.addEventListener("input", renderCases);

renderCases();
