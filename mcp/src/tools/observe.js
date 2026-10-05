// Reading the world: player, camera, entities, models, and vision.
import { tool, vec3, number, integer, boolean, string, entityTypes, entityRef, objectId, model } from './schema.js';

const imageOptions = {
  format: string('Image format.', { enum: ['jpeg', 'png'], default: 'jpeg' }),
  quality: integer('JPEG quality, 1-100.', { default: 80 }),
  max_width: integer('Downscale the image to this width when the game renders wider. Screen coordinates in the ' +
    'other tools stay in game pixels: multiply image pixels by image_to_screen_scale.'),
};

export default [
  tool('get_player',
    'Player state: position [x, y, z], heading (degrees, 0 = north/+Y, counter-clockwise), velocity, health, ' +
    'interior, vehicle if any, and the ground height below.'),

  tool('get_camera',
    'The camera the last frame was rendered with: position, forward/up vectors, heading and pitch in degrees, ' +
    'field of view, clip planes, resolution, and whether it is the game\'s own camera or one placed by set_camera.'),

  tool('get_nearby_entities',
    'Lists world entities (map buildings, objects, vehicles, peds) around a point, nearest first, with their ' +
    'reference, model id and name, position and rotation. Centre defaults to the player.',
    {
      position: vec3('Centre of the search. Defaults to the player (or the camera, see "around").'),
      around: string('Default centre when "position" is not given.', { enum: ['player', 'camera'], default: 'player' }),
      radius: number('Search radius in metres.', { default: 50 }),
      types: entityTypes('Only these kinds of entities. Default: building, vehicle, ped, object.'),
      model: { ...model, description: 'Only entities using this model (id or name).' },
      rendered_only: boolean('Only entities the game currently has a 3D model loaded for.'),
      limit: integer('Maximum number of entities returned.', { default: 50 }),
    }),

  tool('get_entity',
    'Everything about one entity: type, model, position, rotation, flags, LOD, bounding box in model and world ' +
    'space, and its rectangle on screen. Give "entity" (a reference) or "object_id" (an object you created).',
    { entity: entityRef, object_id: objectId }),

  tool('get_entity_bounds',
    'Bounding box of an entity: model-space min/max, size, radius, the eight world-space corners, the ' +
    'world-space axis-aligned box, and the rectangle it covers on screen.',
    { entity: entityRef, object_id: objectId }),

  tool('search_models',
    'Searches the game\'s model catalogue by name (every word must appear in the name; a number matches the ' +
    'model id). Returns id, name, texture dictionary, type and size. Use it to find what to place: ' +
    'e.g. "bench", "tree palm", "barrier".',
    {
      query: string('Words to look for in model names, e.g. "bench" or "road barrier".'),
      type: string('Only this kind of model.', {
        enum: ['object', 'timed_object', 'animated_object', 'weapon', 'vehicle', 'ped', 'lod'],
      }),
      limit: integer('Maximum number of models returned.', { default: 25 }),
    },
    ['query']),

  tool('get_model_info',
    'Details of one model: name, type, texture dictionary, .ide file, draw distance, bounding box and size, ' +
    'whether it is loaded and whether it can be placed as an object.',
    {
      model,
      load: boolean('Load the model first (some bounding boxes only exist once loaded).'),
    },
    ['model']),

  tool('take_screenshot',
    'Returns the frame the game just rendered, as an image. It is taken after the previous commands have ' +
    'taken effect on screen.',
    imageOptions),

  tool('capture_scene',
    'One-call observation of the scene: the screenshot plus the camera (position, heading/pitch, FOV), the ' +
    'entities in view (reference, model id and name, position, rotation, bounding box, screen rectangle, ' +
    'whether something hides them), the selected object, and collision probes (what the screen centre and a ' +
    'grid of screen points hit, and the ground below the camera). Use it to look, decide, then verify an edit.',
    {
      ...imageOptions,
      screenshot: boolean('Include the image (default true). False returns only the data.'),
      radius: number('Only entities within this distance of the camera, in metres.', { default: 150 }),
      max_entities: integer('Maximum number of entities described, nearest first.', { default: 40 }),
      types: entityTypes('Only these kinds of entities. Default: building, vehicle, ped, object.'),
      occlusion: boolean('Test whether each entity is hidden behind something (default true).'),
      probe_grid: integer('Collision probes per screen side: 3 gives a 3x3 grid. 0 disables the grid.', { default: 3 }),
      probe_distance: number('How far the probes reach, in metres.', { default: 500 }),
    }),
];
