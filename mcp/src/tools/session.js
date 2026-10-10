// The game process itself: is it there, and starting it. Handled by the MCP server, not the plugin.
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { tool, integer, string } from './schema.js';
import { GameError } from '../bridge.js';

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function status(bridge) {
  try {
    return { running: true, ...(await bridge.call('get_status', {}, 5000)) };
  } catch (error) {
    if (error.code === 'not_running' || error.code === 'disconnected') {
      return { running: false, ready: false, hint: 'call launch_game, or start gta_sa.exe with gta-sa-mcp.asi installed' };
    }
    throw error;
  }
}

async function launch(bridge, args) {
  let current = await status(bridge);
  if (!current.running) {
    const directory = args.game_dir || process.env.GTA_SA_DIR;
    if (!directory) {
      throw new GameError('not_configured',
        'no game folder: pass game_dir, or set the GTA_SA_DIR environment variable of the MCP server');
    }
    const executable = path.join(directory, 'gta_sa.exe');
    if (!existsSync(executable)) throw new GameError('not_found', `${executable} does not exist`);
    if (!existsSync(path.join(directory, 'gta-sa-mcp.asi'))) {
      throw new GameError('not_installed', `gta-sa-mcp.asi is not in ${directory}: install the plugin first (see the README)`);
    }
    spawn(executable, [], { cwd: directory, detached: true, stdio: 'ignore' }).unref();
  }

  // The plugin answers get_status as soon as it is loaded; "ready" comes once the world is in play.
  const deadline = Date.now() + (args.timeout_seconds ?? 180) * 1000;
  while (Date.now() < deadline) {
    current = await status(bridge);
    if (current.ready) return current;
    await sleep(1000);
  }
  return { ...current, timed_out: true, hint: 'the game did not reach play in time; check gta-sa-mcp.log in the game folder' };
}

export default [
  tool('resize_windowed_client', 'Resize only the selected game process window, preserving desktop position, focus and z-order. Requires windowed/no_activate. Native WM_SIZE processing can be deferred: verify subsequent capture dimensions, then restore previous_width/previous_height.', {width:{type:'integer',minimum:640,maximum:1920},height:{type:'integer',minimum:480,maximum:1080}}, ['width','height']),
  tool('begin_roadsign_trace',
    'Opt-in GTA US1.0 diagnostic: verify and install eleven roadsign CALL observers, then start a fresh 512-event mapped trace file. ' +
    'Changes code only in the test process; preserves native results. Adds timing overhead. Disabled by default; not a crash fix.'),
  tool('get_roadsign_trace',
    'Read completed roadsign call returns, bounded resource fields, overwritten count and mapped trace filename. ' +
    'Stage 3 uses AL (value & 255). No pre-call markers or full nested invocation correlation.'),
  tool('end_roadsign_trace',
    'Stop recording and flush the mapped roadsign trace. Hooks remain installed until game exit; native results remain unchanged.'),
  tool('get_windowed_modes', 'Lists existing native mode indices bounded640..1920x480..1080 for safe windowed requests, plus current index.'),
  tool('change_windowed_mode', 'Requests an existing native GTA video mode through the full RenderWare lifecycle, forcing windowed/no_activate. No exclusive display-mode change. Record previous_mode_index and restore it; inspect reset_history for actual forwarded dimensions and HRESULT.', {mode_index: {type:'integer', minimum:0, maximum:511}}, ['mode_index']),
  tool('reset_windowed_device',
    'Test-only: recreate the current video mode through GTA on the game thread. Requires windowed/no_activate mode. ' +
    'Keeps the desktop out of exclusive fullscreen. Returns actual native Reset call count and HRESULT; inspect subsequent rendering separately.'),
  {
    ...tool('get_status',
      'Whether the game is running and ready for commands, with the plugin version, the game state ' +
      '(intro, main_menu, loading, in_game) and the resolution. Works when the game is not running.'),
    handler: (bridge) => status(bridge),
  },
  {
    ...tool('launch_game',
      'Starts GTA: San Andreas (unless it is already running) and waits until the world is in play. The game ' +
      'starts a sandbox session by itself: no menu to click through.',
      {
        game_dir: string('Folder containing gta_sa.exe and gta-sa-mcp.asi. Defaults to the GTA_SA_DIR environment variable.'),
        timeout_seconds: integer('How long to wait for the game to be ready.', { default: 180 }),
      }),
    handler: launch,
  },
];
