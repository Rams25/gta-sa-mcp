// The objects this session placed, with undo/redo and save/load.
//
// An object keeps its id for its whole life, including across undo/redo (which destroy and
// recreate the game entity behind it). Game thread only.
#pragma once

#include "../core/dispatcher.hpp"
#include "../game/sdk.hpp"

#include <string>
#include <vector>

namespace editor
{

struct Object
{
	int id = 0;
	int model = 0;
	game::Vec3 position, rotation; // rotation: degrees, see game::SetRotation
	std::string name;              // free label given by the caller
	void* entity = nullptr;        // null while deleted (kept for undo)
};

// All of these throw CommandError on failure.
const Object& Create(int model, const game::Vec3& position, const game::Vec3& rotation, const std::string& name);
const Object& Transform(int id, const game::Vec3& position, const game::Vec3& rotation);
void Delete(int id);
const Object& Clone(int id, const game::Vec3& position, const game::Vec3& rotation);

// Live objects only.
const Object* Find(int id);
const Object& Require(int id);
const Object* FindByEntity(void* entity);
std::vector<const Object*> List();

// 0 when nothing is selected.
int Selected();
void Select(int id);

// One step back/forward. Returns what was done, or null when there is nothing to undo/redo.
json Undo();
json Redo();
json HistoryState();

// Writes <game>\gta-sa-mcp\scenes\<name>.json, plus an export in `format` ("pawn", "ipl" or "").
json Save(const std::string& name, const std::string& format);
// Recreates the objects of a saved scene; `replace` deletes the current ones first.
json Load(const std::string& name, bool replace);

}
