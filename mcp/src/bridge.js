// Client side of the plugin's transport: one JSON request per line, one JSON response per line,
// over the named pipe gta-sa-mcp.asi listens on.
import net from 'node:net';

export class GameError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

// GTA_SA_MCP_PIPE: a pipe name ("gta-sa-mcp") or a full path (a socket path in the tests).
export function pipePath(name = process.env.GTA_SA_MCP_PIPE || 'gta-sa-mcp') {
  return name.includes('/') || name.includes('\\') ? name : `\\\\.\\pipe\\${name}`;
}

export class Bridge {
  constructor(path = pipePath()) {
    this.path = path;
    this.socket = null;
    this.connecting = null;
    this.pending = new Map();
    this.nextId = 1;
    this.buffer = '';
  }

  connect() {
    if (this.socket) return Promise.resolve();
    if (this.connecting) return this.connecting;

    this.connecting = new Promise((resolve, reject) => {
      const socket = net.connect(this.path);
      socket.setEncoding('utf8');
      socket.once('connect', () => {
        this.socket = socket;
        this.connecting = null;
        resolve();
      });
      socket.once('error', (error) => {
        if (this.socket === socket) return; // reported through 'close'
        this.connecting = null;
        reject(new GameError('not_running',
          `GTA: San Andreas is not reachable on ${this.path} (${error.code || error.message}). ` +
          'Start the game with gta-sa-mcp.asi installed, or call launch_game.'));
      });
      socket.on('data', (chunk) => this.onData(chunk));
      socket.on('close', () => this.onClose(socket));
    });
    return this.connecting;
  }

  onData(chunk) {
    this.buffer += chunk;
    let newline;
    while ((newline = this.buffer.indexOf('\n')) >= 0) {
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      if (!line) continue;
      let message;
      try {
        message = JSON.parse(line);
      } catch {
        continue;
      }
      const waiter = this.pending.get(message.id);
      if (!waiter) continue;
      this.pending.delete(message.id);
      clearTimeout(waiter.timer);
      if (message.error) waiter.reject(new GameError(message.error.code, message.error.message));
      else waiter.resolve(message.result);
    }
  }

  onClose(socket) {
    if (this.socket !== socket) return;
    this.socket = null;
    this.buffer = '';
    for (const waiter of this.pending.values()) {
      clearTimeout(waiter.timer);
      waiter.reject(new GameError('disconnected', 'the game closed the connection (did it exit or crash?)'));
    }
    this.pending.clear();
  }

  // Runs one plugin command. Rejects with a GameError carrying the plugin's error code.
  async call(method, params = {}, timeoutMs = 30000) {
    await this.connect();
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new GameError('timeout', `the game did not answer '${method}' within ${timeoutMs} ms`));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.socket.write(JSON.stringify({ id, method, params }) + '\n');
    });
  }

  close() {
    if (this.socket) this.socket.destroy();
  }
}
