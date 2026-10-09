// Transport contract only; native input, release edges and SA-MP coexistence
// require a live game. This test never sends desktop input or launches GTA.
import assert from 'node:assert/strict';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

test('bounded input and release reach the selected plugin pipe unchanged', async () => {
  const pipe = process.platform === 'win32' ? `\\\\.\\pipe\\gta-input-test-${process.pid}`
    : path.join(os.tmpdir(), `gta-input-test-${process.pid}.sock`);
  const received = [];
  const sockets = new Set();
  const plugin = net.createServer(socket => {
    sockets.add(socket);
    let buffer = '';
    socket.setEncoding('utf8');
    socket.on('data', data => {
      buffer += data;
      let end;
      while ((end = buffer.indexOf('\n')) >= 0) {
        const request = JSON.parse(buffer.slice(0, end)); buffer = buffer.slice(end + 1);
        received.push(request);
        socket.write(JSON.stringify({ id: request.id, result: { state: request.method } }) + '\n');
      }
    });
  });
  await new Promise(resolve => plugin.listen(pipe, resolve));
  const child = spawn(process.execPath, [fileURLToPath(new URL('../src/index.js', import.meta.url))],
    { env: { ...process.env, GTA_SA_MCP_PIPE: pipe }, stdio: ['pipe', 'pipe', 'inherit'] });
  try {
    let buffer = '', id = 0;
    const pending = new Map();
    child.stdout.setEncoding('utf8');
    child.stdout.on('data', data => {
      buffer += data; let end;
      while ((end = buffer.indexOf('\n')) >= 0) {
        const result = JSON.parse(buffer.slice(0, end)); buffer = buffer.slice(end + 1);
        pending.get(result.id)?.(result); pending.delete(result.id);
      }
    });
    async function call(name, args) {
      const requestId = ++id;
      const response = new Promise(resolve => pending.set(requestId, resolve));
      child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: requestId, method: 'tools/call', params: { name, arguments: args } }) + '\n');
      return response;
    }
    const params = { left_y: -128, buttons: ['cross'], frames: 12, timeout_ms: 900 };
    assert.equal((await call('set_game_input', params)).result.isError, undefined);
    assert.equal(received.at(-1).method, 'set_game_input');
    assert.deepEqual(received.at(-1).params, params);
    await call('release_game_input', {});
    assert.equal(received.at(-1).method, 'release_game_input');
    await call('get_game_input', {});
    assert.equal(received.at(-1).method, 'get_game_input');
  } finally {
    child.kill();
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => plugin.close(resolve));
  }
});
