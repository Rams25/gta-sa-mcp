// Hot reload: swap a model's geometry, textures and collision for files on disk.
#include "common.hpp"

#include "../game/assets.hpp"
#include "../game/catalog.hpp"

namespace
{

json ModelState(int model)
{
	json out = { { "model", model }, { "model_name", game::catalog::NameOf(model) }, { "loaded", game::streaming::IsLoaded(model) } };
	game::Bounds bounds;
	if (game::model::GetBounds(model, bounds))
		out["bounds"] = { { "min", cmd::ToJson(bounds.min) }, { "max", cmd::ToJson(bounds.max) }, { "radius", cmd::Round(bounds.radius) } };
	return out;
}

json LoadModel(const json& params)
{
	if (!params.contains("model"))
		throw CommandError("bad_params", "missing 'model' (an id or a name)");
	const int model = cmd::ResolveModel(params["model"]);
	const std::string dff = params.value("dff", std::string());
	const std::string txd = params.value("txd", std::string());
	const std::string col = params.value("col", std::string());
	if (dff.empty() && txd.empty() && col.empty())
		throw CommandError("bad_params", "give at least one of 'dff', 'txd', 'col' (file paths)");

	const game::assets::Result result = game::assets::Replace(model, dff, txd, col);
	if (!result.ok)
		throw CommandError("load_failed", result.error);

	json out = ModelState(model);
	out["entities_refreshed"] = result.entitiesRefreshed;
	return out;
}

json RestoreModel(const json& params)
{
	if (!params.contains("model"))
		throw CommandError("bad_params", "missing 'model' (an id or a name)");
	const int model = cmd::ResolveModel(params["model"]);
	if (!game::assets::Restore(model))
		throw CommandError("not_replaced", "model " + std::to_string(model) + " was not replaced by load_model");
	json out = ModelState(model);
	out["note"] = "geometry and textures are the game's again; the collision returns when the area's collision is reloaded (move away and back, or restart)";
	return out;
}

}

void RegisterAssetCommands()
{
	dispatcher::Register("load_model", Phase::Tick, LoadModel, true);
	dispatcher::Register("restore_model", Phase::Tick, RestoreModel, true);
}
