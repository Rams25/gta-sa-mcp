// The tool catalogue. A tool without a handler is forwarded as it is to the plugin command of the
// same name: adding a command in the .asi only needs its description here.
import session from './session.js';
import observe from './observe.js';
import control from './control.js';
import query from './query.js';
import edit from './edit.js';

export const tools = [...session, ...observe, ...control, ...query, ...edit];

const byName = new Map(tools.map((entry) => [entry.name, entry]));

// Screenshots take a frame or two to come back, and a first model load can stall the game thread.
const TIMEOUT_MS = 45000;

export function findTool(name) {
  return byName.get(name);
}

// Runs a tool and returns MCP content blocks: the image first when there is one, then the data.
export async function runTool(bridge, entry, args) {
  const result = entry.handler
    ? await entry.handler(bridge, args)
    : await bridge.call(entry.name, args, TIMEOUT_MS);

  const content = [];
  let data = result;
  if (result && typeof result === 'object' && result.image && result.image.base64) {
    const { base64, mime_type: mimeType, ...imageInfo } = result.image;
    content.push({ type: 'image', data: base64, mimeType });
    data = { ...result, image: imageInfo };
  }
  content.push({ type: 'text', text: JSON.stringify(data) });
  return content;
}
