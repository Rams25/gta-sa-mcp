#!/usr/bin/env node
// Runs one tool against the running game, without an MCP client. Handy while developing:
//   node mcp/scripts/call.mjs get_player
//   node mcp/scripts/call.mjs create_object '{"model":"parkbench1","position":[2495,-1672,14],"snap_to_ground":true}'
//   node mcp/scripts/call.mjs capture_scene '{}' shot      -> also writes shot.jpg in the current folder
import { writeFileSync } from 'node:fs';
import path from 'node:path';
import { Bridge } from '../src/bridge.js';
import { findTool, runTool } from '../src/tools/index.js';

const [name, args = '{}', imageName = 'screenshot'] = process.argv.slice(2);
const entry = findTool(name);
if (!entry) {
  console.error(`Unknown tool: ${name ?? '(none)'}`);
  process.exit(2);
}

const bridge = new Bridge();
try {
  for (const block of await runTool(bridge, entry, JSON.parse(args))) {
    if (block.type === 'image') {
      const file = path.resolve(`${imageName}.${block.mimeType === 'image/png' ? 'png' : 'jpg'}`);
      writeFileSync(file, Buffer.from(block.data, 'base64'));
      console.log(`[image saved] ${file}`);
    } else {
      console.log(block.text);
    }
  }
} catch (error) {
  console.error(`ERROR ${error.code ?? ''} ${error.message}`);
  process.exitCode = 1;
} finally {
  bridge.close();
}
