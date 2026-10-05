// Moving around: the player, the camera, and the look of the world.
import { tool, vec3, number, integer, boolean, entityRef, objectId } from './schema.js';

export default [
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
