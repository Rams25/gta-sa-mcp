// Small builders for the tools' JSON schemas, and the default way a tool maps to a plugin command.

export const vec3 = (description) => ({
  type: 'array',
  items: { type: 'number' },
  minItems: 3,
  maxItems: 3,
  description,
});

export const number = (description, extra = {}) => ({ type: 'number', description, ...extra });
export const integer = (description, extra = {}) => ({ type: 'integer', description, ...extra });
export const boolean = (description) => ({ type: 'boolean', description });
export const string = (description, extra = {}) => ({ type: 'string', description, ...extra });

export const entityTypes = (description) => ({
  type: 'array',
  items: { type: 'string', enum: ['building', 'vehicle', 'ped', 'object', 'dummy'] },
  description,
});

export const entityRef = string('Entity reference as returned by the other tools, e.g. "building:118272".');
export const objectId = integer('Id of an object created in this session (object_id). Defaults to the selected object.');
export const model = {
  type: ['integer', 'string'],
  description: 'Model id (e.g. 1337) or model name from the game files (e.g. "BinNt07_LA"). Find them with search_models.',
};

// A tool whose arguments are passed as they are to the plugin command of the same name.
export function tool(name, description, properties = {}, required = []) {
  return {
    name,
    description,
    inputSchema: { type: 'object', properties, required, additionalProperties: false },
  };
}
