#!/usr/bin/env node
// MCP server (stdio) for GTA: San Andreas. It speaks the Model Context Protocol to the agent and
// forwards tool calls to gta-sa-mcp.asi inside the game. No dependencies: MCP over stdio is one
// JSON-RPC 2.0 message per line.
import readline from 'node:readline';
import { Bridge } from './bridge.js';
import { tools, findTool, runTool } from './tools/index.js';

const SERVER = { name: 'gta-sa-mcp', version: '0.1.0' };
const PROTOCOL_VERSIONS = ['2025-06-18', '2025-03-26', '2024-11-05'];

const INSTRUCTIONS = `Controls a running GTA: San Andreas through an in-game plugin.

Loop: observe (capture_scene) -> decide -> edit (create_object, move_object, rotate_object...) -> verify
(capture_scene or take_screenshot) -> correct (undo, move...) -> save_changes.

Conventions:
- Positions are [x, y, z] in metres; z is up; +y is north, +x is east.
- Headings are degrees, 0 = north, counter-clockwise (90 = west). Pitch is positive upwards.
- Object rotations are [rx, ry, rz] degrees, as in SA-MP's CreateObject; rz turns around the vertical.
- Screen coordinates are game pixels from the top-left corner (see "resolution"); a downscaled
  screenshot reports image_to_screen_scale.
- World entities are named by a reference such as "building:118272"; objects you create have an object_id.
- Collision (raycast, get_ground_z, snap_to_ground, is_position_free) only exists near the player:
  teleport close to where you work. The camera can be anywhere (set_camera, look_at).
- If a tool says the game is not running, call launch_game; if it says not ready, wait and retry.`;

const bridge = new Bridge();

function send(message) {
  process.stdout.write(JSON.stringify(message) + '\n');
}

async function callTool(params) {
  const entry = findTool(params?.name);
  if (!entry) return { isError: true, content: [{ type: 'text', text: `Unknown tool: ${params?.name}` }] };
  try {
    return { content: await runTool(bridge, entry, params.arguments ?? {}) };
  } catch (error) {
    const code = error.code ? `[${error.code}] ` : '';
    return { isError: true, content: [{ type: 'text', text: `${code}${error.message}` }] };
  }
}

async function handle(message) {
  switch (message.method) {
    case 'initialize': {
      const requested = message.params?.protocolVersion;
      return {
        protocolVersion: PROTOCOL_VERSIONS.includes(requested) ? requested : PROTOCOL_VERSIONS[0],
        capabilities: { tools: {} },
        serverInfo: SERVER,
        instructions: INSTRUCTIONS,
      };
    }
    case 'ping':
      return {};
    case 'tools/list':
      return { tools: tools.map(({ name, description, inputSchema }) => ({ name, description, inputSchema })) };
    case 'tools/call':
      return callTool(message.params);
    default:
      throw Object.assign(new Error(`Method not found: ${message.method}`), { rpcCode: -32601 });
  }
}

const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
input.on('line', async (line) => {
  if (!line.trim()) return;
  let message;
  try {
    message = JSON.parse(line);
  } catch {
    send({ jsonrpc: '2.0', id: null, error: { code: -32700, message: 'Parse error' } });
    return;
  }
  // Notifications (no id) need no answer.
  if (message.id === undefined || message.id === null) return;
  try {
    send({ jsonrpc: '2.0', id: message.id, result: await handle(message) });
  } catch (error) {
    send({ jsonrpc: '2.0', id: message.id, error: { code: error.rpcCode ?? -32603, message: error.message } });
  }
});
input.on('close', () => {
  bridge.close();
  process.exit(0);
});
