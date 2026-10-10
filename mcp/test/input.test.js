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
    const keyLease = { key: 'RIGHT', duration_ms: 650 };
    assert.equal((await call('set_samp_key', keyLease)).result.isError, undefined);
    assert.equal(received.at(-1).method, 'set_samp_key');
    assert.deepEqual(received.at(-1).params, keyLease);
    for (const [name, args] of [
      ['submit_samp_command',{text:'/help'}],
      ['samp_dropped_pickup_fixture',{action:'read'}],
      ['samp_ui_event',{element:'dialog',event:'key',key:'DOWN'}],
      ['samp_ui_event',{element:'dialog',event:'double_click',x:20,y:25}],
      ['samp_ui_event',{element:'deathlist',event:'key',key:'F9'}],
      ['samp_ui_event',{element:'dialog',event:'type_fixture',sample:'password_long'}],
      ['samp_ui_event',{element:'textdraw',event:'hover',x:320,y:240}],
      ['resize_windowed_client',{width:1024,height:768}],
    ]) {
      assert.equal((await call(name,args)).result.isError,undefined);
      assert.equal(received.at(-1).method,name);assert.deepEqual(received.at(-1).params,args);
    }
    const ui = { element: 'scoreboard', open: true };
    assert.equal((await call('set_samp_ui', ui)).result.isError, undefined);
    assert.equal(received.at(-1).method, 'set_samp_ui');
    assert.deepEqual(received.at(-1).params, ui);
    await call('get_windowed_modes', {});
    assert.equal(received.at(-1).method, 'get_windowed_modes');
    await call('change_windowed_mode', { mode_index: 1 });
    assert.equal(received.at(-1).method, 'change_windowed_mode');
    await call('get_samp_key', {});
    assert.equal(received.at(-1).method, 'get_samp_key');
    await call('release_samp_key', {});
    assert.equal(received.at(-1).method, 'release_samp_key');
  } finally {
    child.kill();
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => plugin.close(resolve));
  }
});
