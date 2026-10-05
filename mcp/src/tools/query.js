// Geometry questions: rays, projections, ground and free space.
import { tool, vec3, number, boolean, string, integer, entityTypes } from './schema.js';

const screenPoint = {
  x: number('Horizontal screen coordinate, in game pixels from the left (see "resolution" in get_camera).'),
  y: number('Vertical screen coordinate, in game pixels from the top.'),
  normalized: boolean('Read x and y as fractions of the screen (0-1) instead of pixels.'),
};

export default [
  tool('raycast',
    'Casts a ray against the world\'s collision. Returns the first hit: position, surface normal, distance and ' +
    'the entity hit. Give "from" and either "to" or "direction" (+ "distance"). Collision only exists around ' +
    'the player.',
    {
      from: vec3('Start of the ray [x, y, z].'),
      to: vec3('End of the ray [x, y, z].'),
      direction: vec3('Direction of the ray (any length), used when "to" is not given.'),
      distance: number('Length of the ray when "direction" is used, in metres.', { default: 300 }),
      types: entityTypes('What the ray can hit. Default: building, vehicle, ped, object.'),
    },
    ['from']),

  tool('screen_to_world',
    'What is under a screen pixel: casts a ray from the camera through it and returns the world position hit, ' +
    'the surface normal and the entity. Use it to turn "there, in the screenshot" into coordinates.',
    {
      ...screenPoint,
      max_distance: number('How far to look, in metres.', { default: 1000 }),
      types: entityTypes('What the ray can hit. Default: building, vehicle, ped, object.'),
    },
    ['x', 'y']),

  tool('world_to_screen',
    'Where a world position appears on screen: pixel coordinates, depth, and whether it is in front of the ' +
    'camera and inside the frame. Give "position" or several "positions".',
    {
      position: vec3('World position [x, y, z].'),
      positions: { type: 'array', items: vec3('World position [x, y, z].'), description: 'Several positions at once.' },
    }),

  tool('get_ground_z',
    'Height of the ground at or below a point. Give "position", or "x" and "y" (searching down from z, 1000 by ' +
    'default). Collision only exists around the player.',
    {
      position: vec3('Point to search down from [x, y, z].'),
      x: number('X coordinate.'),
      y: number('Y coordinate.'),
      z: number('Height to search down from.', { default: 1000 }),
    }),

  tool('is_position_free',
    'Tests whether a sphere at a position overlaps anything solid, and names what blocks it. Use it before ' +
    'placing an object, with a radius close to the object\'s size.',
    {
      position: vec3('Centre of the sphere [x, y, z].'),
      radius: number('Radius of the sphere in metres.', { default: 1 }),
      ignore_object_id: integer('One of your objects to leave out of the test (the one being placed).'),
      ignore_entity: string('Entity reference to leave out of the test.'),
      types: entityTypes('What counts as an obstacle. Default: building, vehicle, ped, object.'),
    },
    ['position']),
];
