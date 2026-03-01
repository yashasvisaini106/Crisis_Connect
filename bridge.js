/*
  CrisisConnect WebSocket-TCP bridge
  - Accepts WebSocket clients on ws://localhost:3000
  - Connects to C++ TCP server on 127.0.0.1:8080
  - Relays newline-delimited messages both directions
*/

const net = require("net");
const { WebSocketServer } = require("ws");

const TCP_HOST = "127.0.0.1";
const TCP_PORT = 8080;
const WS_PORT = 3000;

function safeTrim(value) {
  return (value || "").toString().trim();
}

function sendTcpLine(tcpClient, line) {
  if (!line) {
    return;
  }
  tcpClient.write(`${line}\n`);
}

const wss = new WebSocketServer({ port: WS_PORT });
console.log(`Bridge listening for web clients on ws://localhost:${WS_PORT}`);

wss.on("connection", (ws) => {
  const tcpClient = new net.Socket();
  let tcpBuffer = "";

  tcpClient.connect(TCP_PORT, TCP_HOST, () => {
    ws.send(JSON.stringify({
      type: "system",
      timestamp: new Date().toISOString(),
      level: "info",
      text: "Connected to C++ crisis server through bridge."
    }));
  });

  ws.on("message", (payload) => {
    const raw = payload.toString();
    let data = null;

    try {
      data = JSON.parse(raw);
    } catch {
      sendTcpLine(tcpClient, safeTrim(raw));
      return;
    }

    const action = safeTrim(data.action).toLowerCase();

    if (action === "profile") {
      const name = safeTrim(data.name);
      const role = safeTrim(data.role).toUpperCase();
      const location = safeTrim(data.location);

      if (name) sendTcpLine(tcpClient, `/name ${name}`);
      if (role) sendTcpLine(tcpClient, `/role ${role}`);
      if (location) sendTcpLine(tcpClient, `/location ${location}`);
      return;
    }

    if (action === "chat") {
      sendTcpLine(tcpClient, safeTrim(data.text));
      return;
    }

    if (action === "command") {
      sendTcpLine(tcpClient, safeTrim(data.value));
      return;
    }

    if (raw) {
      sendTcpLine(tcpClient, safeTrim(raw));
    }
  });

  tcpClient.on("data", (data) => {
    tcpBuffer += data.toString();

    while (true) {
      const newlineIndex = tcpBuffer.indexOf("\n");
      if (newlineIndex === -1) {
        break;
      }

      const line = tcpBuffer.slice(0, newlineIndex).trim();
      tcpBuffer = tcpBuffer.slice(newlineIndex + 1);
      if (!line) {
        continue;
      }

      if (ws.readyState === 1) {
        ws.send(line);
      }
    }
  });

  ws.on("close", () => {
    tcpClient.destroy();
  });

  tcpClient.on("close", () => {
    if (ws.readyState === 1) {
      ws.close();
    }
  });

  tcpClient.on("error", (err) => {
    if (ws.readyState === 1) {
      ws.send(JSON.stringify({
        type: "system",
        timestamp: new Date().toISOString(),
        level: "error",
        text: `Bridge TCP error: ${err.message}`
      }));
      ws.close();
    }
  });
});
