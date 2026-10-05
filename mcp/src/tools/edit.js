// Changing the world: objects, their history, and their files.
import { tool, vec3, boolean, string, objectId, model } from './schema.js';

const rotation = (description) => vec3(description +
  ' Degrees [rx, ry, rz], the convention of SA-MP\'s CreateObject and MTA: rz turns around the vertical axis.');

export default [
  tool('create_object',
    'Places a static object in the world and selects it. Returns its object_id (used by the other object ' +
    'tools), its bounding box and where it is. Find models with search_models.',
    {
      model,
      position: vec3('Where to put the object\'s origin [x, y, z].'),
      rotation: rotation('Orientation.'),
      snap_to_ground: boolean('Lower (or raise) the object so the bottom of its bounding box rests on the ground below the position.'),
      name: string('A label of your own to recognise the object later.'),
      select: boolean('Select the new object (default true).'),
    },
    ['model', 'position']),

  tool('move_object',
    'Moves an object. Give "position" (absolute) or "offset" (relative, along the world axes or the object\'s ' +
    'own axes with "local").',
    {
      object_id: objectId,
      position: vec3('New position [x, y, z].'),
      offset: vec3('Displacement [dx, dy, dz] from the current position.'),
      local: boolean('Interpret "offset" along the object\'s own axes (x = its right, y = its front, z = its top).'),
      snap_to_ground: boolean('After moving, rest the bottom of the bounding box on the ground below.'),
    }),

  tool('rotate_object',
    'Rotates an object. Give "rotation" (absolute) or "delta" (added to the current angles).',
    {
      object_id: objectId,
      rotation: rotation('New orientation.'),
      delta: vec3('Degrees to add [drx, dry, drz]; [0, 0, 90] turns the object a quarter turn counter-clockwise seen from above.'),
    }),

  tool('delete_object', 'Removes an object you created. Can be undone.', { object_id: objectId }),

  tool('clone_object',
    'Duplicates an object (same model, rotation and label) and selects the copy. Give "position" or "offset" ' +
    'to place the copy elsewhere.',
    {
      object_id: objectId,
      position: vec3('Position of the copy [x, y, z].'),
      offset: vec3('Displacement of the copy from the original [dx, dy, dz].'),
      rotation: rotation('Orientation of the copy, if different.'),
      select: boolean('Select the copy (default true).'),
    }),

  tool('select_object',
    'Selects one of your objects: the object tools then apply to it when "object_id" is omitted, and ' +
    'capture_scene reports it. Omit object_id to clear the selection.',
    { object_id: objectId }),

  tool('list_objects',
    'Lists the objects created in this session with their model, position and rotation, plus the selection ' +
    'and the number of undo/redo steps.'),

  tool('undo', 'Reverts the last change made to your objects (create, move, rotate, delete, clone, load).'),
  tool('redo', 'Reapplies the last change reverted by undo.'),

  tool('save_changes',
    'Saves the objects you created to <game folder>\\gta-sa-mcp\\scenes\\<name>.json (reloadable with ' +
    'load_changes) and, with "format", also exports them for use elsewhere. Returns the paths of the files written.',
    {
      name: string('Scene name: letters, digits, "-" and "_".', { default: 'scene' }),
      format: string('Extra export: "pawn" writes CreateObject(...) lines for SA-MP/open.mp, "ipl" writes a map placement file.',
        { enum: ['json', 'pawn', 'ipl'] }),
    }),

  tool('load_model',
    'Hot reload: replaces the geometry (DFF), textures (TXD) and/or collision (COL) of a model with files on ' +
    'disk, without restarting the game. Every instance of the model in the world shows the new version on the ' +
    'next frame. Use it to check a model exported from a 3D tool in the real game, then export and reload again.',
    {
      model,
      dff: string('Absolute path of the .dff file (the model itself).'),
      txd: string('Absolute path of the .txd file holding the textures the DFF names.'),
      col: string('Absolute path of a .col file (COL2 or COL3) holding one collision model.'),
      transparent: boolean('The model has see-through parts (glass): draw it after the opaque world so what is ' +
        'behind the glass stays visible. Equivalent to the "draw last" flag of the .ide.'),
    },
    ['model']),

  tool('restore_model',
    'Undoes load_model: the geometry and textures of the model come from the game archives again.',
    { model },
    ['model']),

  tool('load_changes',
    'Recreates the objects of a scene saved with save_changes. One undo reverts the whole load.',
    {
      name: string('Scene name.', { default: 'scene' }),
      replace: boolean('Delete the current objects first (default true). False adds the scene to them.'),
    }),
];
