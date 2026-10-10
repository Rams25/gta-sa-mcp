// Runs the MCP server against a fake plugin listening on a pipe, and talks MCP to it over stdio.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { after, before, test } from 'node:test';
import { fileURLToPath } from 'node:url';

const pipe = process.platform === 'win32'
  ? `\\\\.\\pipe\\gta-sa-mcp-test-${process.pid}`
  : path.join(os.tmpdir(), `gta-sa-mcp-test-${process.pid}.sock`);

const received = [];
let fakePlugin;
let server;
let nextId = 1;
const waiting = new Map();

// Answers like gta-sa-mcp.asi would.
function answer(request) {
  received.push(request);
  switch (request.method) {
    case 'get_status':
      return { result: { plugin: 'gta-sa-mcp', ready: true, game_state: 'in_game' } };
    case 'take_screenshot':
      return { result: { image: { base64: 'QUJD', mime_type: 'image/jpeg', width: 4, height: 2 } } };
    case 'create_object':
      return { result: { object_id: 1, model: request.params.model, position: request.params.position } };
    case 'delete_object':
      return { error: { code: 'unknown_object', message: 'no object with object_id 99' } };
    default:
      return { error: { code: 'unknown_method', message: request.method } };
  }
}

function rpc(method, params) {
  const id = nextId++;
  server.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
  return new Promise((resolve) => waiting.set(id, resolve));
}

before(async () => {
  fakePlugin = net.createServer((socket) => {
    let buffer = '';
    socket.setEncoding('utf8');
    socket.on('data', (chunk) => {
      buffer += chunk;
      let newline;
      while ((newline = buffer.indexOf('\n')) >= 0) {
        const request = JSON.parse(buffer.slice(0, newline));
        buffer = buffer.slice(newline + 1);
        socket.write(JSON.stringify({ id: request.id, ...answer(request) }) + '\n');
      }
    });
  });
  await new Promise((resolve) => fakePlugin.listen(pipe, resolve));

  const entry = fileURLToPath(new URL('../src/index.js', import.meta.url));
  server = spawn(process.execPath, [entry], { env: { ...process.env, GTA_SA_MCP_PIPE: pipe }, stdio: ['pipe', 'pipe', 'inherit'] });
  let buffer = '';
  server.stdout.setEncoding('utf8');
  server.stdout.on('data', (chunk) => {
    buffer += chunk;
    let newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
      const message = JSON.parse(buffer.slice(0, newline));
      buffer = buffer.slice(newline + 1);
      waiting.get(message.id)?.(message);
      waiting.delete(message.id);
    }
  });
});

after(() => {
  server.kill();
  fakePlugin.close();
});

test('initialize negotiates the protocol version', async () => {
  const { result } = await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'test', version: '0' } });
  assert.equal(result.protocolVersion, '2024-11-05');
  assert.equal(result.serverInfo.name, 'gta-sa-mcp');
  assert.ok(result.capabilities.tools);
});

test('tools/list exposes every required tool with a schema', async () => {
  const { result } = await rpc('tools/list', {});
  const names = result.tools.map((entry) => entry.name);
  const required = [
    'get_player', 'get_camera', 'set_camera', 'look_at', 'teleport', 'get_nearby_entities', 'get_entity',
    'get_entity_bounds', 'create_object', 'move_object', 'rotate_object', 'delete_object', 'clone_object',
    'raycast', 'screen_to_world', 'world_to_screen', 'get_ground_z', 'is_position_free', 'search_models',
    'get_model_info', 'take_screenshot', 'capture_scene', 'save_changes', 'undo', 'redo',
  ];
  for (const name of required) assert.ok(names.includes(name), `missing tool ${name}`);
  assert.equal(new Set(names).size, names.length, 'duplicate tool names');
  const byName = Object.fromEntries(result.tools.map(entry => [entry.name, entry]));
  assert.deepEqual(byName.set_samp_key.inputSchema.properties.key.enum, ['LEFT', 'RIGHT', 'SHIFT'], 'UI fixtures must not broaden the polling-key allowlist');
  const ui = byName.samp_ui_event.inputSchema.properties;
  assert.ok(ui.element.enum.includes('deathlist') && ui.key.enum.includes('F9'));
  assert.ok(ui.event.enum.includes('hover') && ui.sample.enum.includes('password_long'));
  assert.match(byName.samp_ui_event.description, /password_long uses native SetText.*272.*ordinary typing caps128/, 'long fixture must disclose its native setter path');
  assert.equal(ui.x.minimum, 0); assert.equal(ui.x.maximum, 1920);
  assert.equal(ui.y.minimum, 0); assert.equal(ui.y.maximum, 1080);
  for (const entry of result.tools) {
    assert.equal(entry.inputSchema.type, 'object', entry.name);
    assert.ok(entry.description.length > 20, entry.name);
  }
});

test('a tool call is forwarded to the plugin with its arguments', async () => {
  const { result } = await rpc('tools/call', { name: 'create_object', arguments: { model: 1337, position: [1, 2, 3] } });
  assert.ok(!result.isError);
  assert.deepEqual(JSON.parse(result.content[0].text), { object_id: 1, model: 1337, position: [1, 2, 3] });
  assert.deepEqual(received.at(-1).params, { model: 1337, position: [1, 2, 3] });
});

test('a screenshot comes back as an image block followed by its data', async () => {
  const { result } = await rpc('tools/call', { name: 'take_screenshot', arguments: {} });
  assert.deepEqual(result.content[0], { type: 'image', data: 'QUJD', mimeType: 'image/jpeg' });
  assert.deepEqual(JSON.parse(result.content[1].text), { image: { width: 4, height: 2 } });
});

test('a plugin error becomes a tool error with its code', async () => {
  const { result } = await rpc('tools/call', { name: 'delete_object', arguments: { object_id: 99 } });
  assert.equal(result.isError, true);
  assert.match(result.content[0].text, /\[unknown_object\] no object with object_id 99/);
});

test('get_status reports a running game', async () => {
  const { result } = await rpc('tools/call', { name: 'get_status', arguments: {} });
  assert.deepEqual(JSON.parse(result.content[0].text), { running: true, plugin: 'gta-sa-mcp', ready: true, game_state: 'in_game' });
});

test('unknown tools and methods are reported', async () => {
  const call = await rpc('tools/call', { name: 'nope', arguments: {} });
  assert.equal(call.result.isError, true);
  const method = await rpc('nope/nope', {});
  assert.equal(method.error.code, -32601);
});
