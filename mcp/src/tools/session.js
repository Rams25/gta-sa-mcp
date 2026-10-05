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
