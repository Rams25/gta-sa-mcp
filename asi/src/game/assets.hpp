// Replacing a model's geometry, textures and collision from loose files while the game runs.
//
// The game normally streams all three from its archives. Here they are read from files on disk and
// installed in place of the model's own, so a model being edited can be reloaded without restarting.
#pragma once

#include "sdk.hpp"

#include <string>

namespace game::assets
{

struct Result
{
	bool ok = false;
	std::string error;
	int txdSlot = -1;
	int entitiesRefreshed = 0;
};

// Any of the three paths may be empty (that part is left as it is). The model must already exist.
Result Replace(int model, const std::string& dff, const std::string& txd, const std::string& col);

// Gives the model's geometry and textures back to the game's streaming. Its collision returns
// when the game next reloads the area's collision file.
bool Restore(int model);

// Game thread, every frame: the game reloads area collision files as the player moves, which
// overwrites a replaced collision model; this puts ours back.
void Tick();

}
