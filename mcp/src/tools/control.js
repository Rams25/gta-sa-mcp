// Moving around: the player, the camera, and the look of the world.
import { tool, vec3, number, integer, boolean, entityRef, objectId } from './schema.js';

export default [
  tool('submit_samp_command', 'Submit fixed /help, /shop or /kill through the actual SA-MP chat edit control and ProcessInput. Game thread, exact build pin, no OS input. Requires no visible dialog.', {text:{type:'string',enum:['/help','/shop','/kill']}}, ['text']),
  tool('samp_ui_event', 'Actual native list keyboard/mouse events or dialog keyboard acceptance. Exact build pin; no OS input. Mouse coordinates are relative to the owning DXUT dialog, not the desktop. Double-click performs down/up/double-click/up through HandleMouse; inspect server response.', {element:{type:'string',enum:['dialog','scoreboard']},event:{type:'string',enum:['accept','cancel','key','wheel','double_click']},key:{type:'string',enum:['HOME','END','UP','DOWN','PAGEUP','PAGEDOWN']},x:{type:'integer',minimum:0,maximum:1920},y:{type:'integer',minimum:0,maximum:1080},steps:{type:'integer',minimum:-10,maximum:10}}, ['element','event']),
  tool('set_samp_ui', 'Fixed per-process SA-MP UI dispatch on the game thread, exact-build allowlist. Scoreboard/chat call the real open/close methods; help calls the original help routine and closes only local help. Netstats holds F5 polling for a bounded lease, then releases. Requires windowed/no_activate; never sends desktop input. Inspect capture for visible acceptance.', {
    element: { type: 'string', enum: ['scoreboard', 'chat', 'help', 'netstats'] },
    open: boolean('Open or close the selected UI.'),
    duration_ms: integer('Netstats lease only, default1000ms.', {minimum:1,maximum:5000}),
  }, ['element','open']),
  tool('invoke_samp_headmove', 'Invokes the normally registered /headmove handler on the selected game thread. Fixed test command for pinned original DL-R1 and phase324 builds only; refuses unknown builds. No OS input or direct flag write. Changes and persists the client head-movement setting; does not simulate chat text entry or recall.'),
  tool('set_samp_key', 'Holds LEFT, RIGHT or SHIFT for SA-MP direct key polling in this process only. ' +
    'Hooks only samp.dll GetAsyncKeyState import; no OS input, focus or desktop actions. ' +
    'The other two exposed keys are neutral during the lease; other keys forward unchanged. Idle class keys are neutral when input.isolate_physical is enabled. ' +
    'Does not bypass dialogue or class-selection gates. Returns active state and observed polls.', {
      key: { type: 'string', enum: ['LEFT', 'RIGHT', 'SHIFT'], description: 'Virtual key to hold.' },
      duration_ms: integer('Wall-clock lease duration, default150ms; replaces any prior lease.', { minimum: 1, maximum: 5000 }),
    }, ['key']),
  tool('release_samp_key', 'Immediately cancels the SA-MP key lease, including while the simulation is blocked. Idle class keys stay neutral if physical isolation is enabled; otherwise normal polling resumes.'),
  tool('get_samp_key', 'Returns SA-MP key hook availability, active state, time remaining, overridden and pressed poll counts.'),
  tool('set_game_input',
    'Drives genuine local GTA pad 0 controls in this connected game process, without desktop focus or OS input. ' +
    'Replaces physical pad input for a bounded number of simulation frames, then releases automatically. ' +
    'Uses raw GTA controller fields: actions depend on the game control mode (square usually jump/brake, ' +
    'cross sprint/accelerate, triangle enter/exit). Does not teleport or bypass disabled controls. ' +
    'Returns scheduling status; use get_game_input to observe completion. Frozen simulation cannot apply input.',
    {
      left_x: integer('Movement/steering axis, negative left, positive right.', { minimum: -128, maximum: 128 }),
      left_y: integer('Movement axis, negative forward, positive backward.', { minimum: -128, maximum: 128 }),
      right_x: integer('Raw right-stick horizontal axis.', { minimum: -128, maximum: 128 }),
      right_y: integer('Raw right-stick vertical axis.', { minimum: -128, maximum: 128 }),
      buttons: { type: 'array', uniqueItems: true, items: { type: 'string', enum: [
        'left_shoulder1', 'left_shoulder2', 'right_shoulder1', 'right_shoulder2',
        'dpad_up', 'dpad_down', 'dpad_left', 'dpad_right', 'select', 'square', 'triangle',
        'cross', 'circle', 'shock_left', 'shock_right', 'walk', 'vehicle_mouse_look', 'radio_track_skip',
      ] }, description: 'Buttons held at native value 128; omitted buttons and axes are neutral.' },
      frames: integer('Simulation frames to apply (default 1). A new command replaces the current lease.', { minimum: 1, maximum: 300 }),
      timeout_ms: integer('Wall-clock expiration checked before each injection (default 5000).', { minimum: 1, maximum: 10000 }),
    }),
  tool('release_game_input', 'Cancels this process\'s input lease; a neutral sample is applied at the next simulation pad update.'),
  tool('get_game_input', 'Reports whether the verified native pad hook is available, remaining/applied frames, and completion/cancellation reason.'),
  tool('teleport',
    'Moves the player (with their vehicle, if any) to a position and loads the map around it. Collision only ' +
    'exists around the player: teleport near a place before using raycast, get_ground_z or snap_to_ground there.',
    {
      position: vec3('Destination [x, y, z].'),
      heading: number('Facing direction in degrees (0 = north/+Y, counter-clockwise).'),
      snap_to_ground: boolean('Put the player on the ground found at or just below the given position.'),
      load_scene: boolean('Load the map around the destination before moving (default true).'),
    },
    ['position']),

  tool('set_camera',
    'Places a fixed camera, independent of the player. Give "position" and either "target" (a point to look ' +
    'at) or "heading"/"pitch" in degrees; anything omitted keeps its current value. {"reset": true} gives the ' +
    'camera back to the game. The world streams in around the camera, so it can be far from the player.',
    {
      position: vec3('Camera position [x, y, z].'),
      target: vec3('Point to look at [x, y, z].'),
      heading: number('Direction in degrees: 0 = north (+Y), 90 = west (-X), counter-clockwise.'),
      pitch: number('Degrees above (+) or below (-) the horizon. -90 looks straight down.'),
      fov: number('Field of view in degrees, as the game counts it (10-140, default 70).'),
      load_scene: boolean('Load the map around the camera now, blocking, instead of letting it stream in.'),
      reset: boolean('Return to the game\'s own camera following the player.'),
    }),

  tool('look_at',
    'Points the camera at a point, an entity or one of your objects. Without "position" the camera stays ' +
    'where it is; with "distance" it is placed that far from the target, seen from "heading"/"pitch" ' +
    '(e.g. distance 15, pitch -30 for a three-quarter view from above).',
    {
      target: vec3('Point to look at [x, y, z].'),
      entity: entityRef,
      object_id: objectId,
      position: vec3('Camera position [x, y, z].'),
      distance: number('Place the camera this far from the target instead of keeping its position.'),
      heading: number('With "distance": direction the camera looks in, degrees (0 = north).', { default: 0 }),
      pitch: number('With "distance": degrees below (-) or above (+) the horizon.', { default: -30 }),
      fov: number('Field of view in degrees (default 70).'),
      load_scene: boolean('Load the map around the camera now, blocking.'),
    }),

  tool('set_world',
    'Sets what the scene looks like: time of day, weather, HUD. Returns the current values.',
    {
      hour: integer('Hour, 0-23.'),
      minute: integer('Minute, 0-59.'),
      freeze_time: boolean('Stop (true) or resume (false) the game clock.'),
      weather: integer('Weather id, 0-20: 0-4 Los Santos (1 = sunny, 4 = cloudy), 5-9 San Fierro (8 = rain, 9 = fog), ' +
        '10-12 Las Venturas, 13-16 countryside (16 = rain), 17-19 desert (19 = sandstorm).'),
      hud: boolean('Show the radar, health bar and other HUD elements.'),
    }),
];
