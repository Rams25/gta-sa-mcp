#include "common.hpp"

#include "../game/catalog.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{

namespace catalog = game::catalog;

const char* TypeName(int type)
{
	switch (type)
	{
	case game::kModelAtomic: return "object";
	case game::kModelTime: return "timed_object";
	case game::kModelWeapon: return "weapon";
	case game::kModelClump: return "animated_object";
	case game::kModelVehicle: return "vehicle";
	case game::kModelPed: return "ped";
	case game::kModelLod: return "lod";
	default: return "other";
	}
}

json BoundsOf(int model)
{
	game::Bounds bounds;
	if (!game::model::GetBounds(model, bounds))
		return nullptr;
	return {
		{ "min", cmd::ToJson(bounds.min) },
		{ "max", cmd::ToJson(bounds.max) },
		{ "size", cmd::ToJson(bounds.max - bounds.min) },
		{ "centre", cmd::ToJson(bounds.centre) },
		{ "radius", cmd::Round(bounds.radius) },
	};
}

json Brief(const catalog::Entry& entry)
{
	json out = { { "model", entry.id }, { "name", entry.name }, { "txd", entry.txd } };
	void* info = game::model::Info(entry.id);
	out["type"] = info ? TypeName(game::model::Type(info)) : "missing";
	game::Bounds bounds;
	if (game::model::GetBounds(entry.id, bounds))
		out["size"] = cmd::ToJson(bounds.max - bounds.min, 2);
	return out;
}

// Every word of the query must appear in the name; '*' and spaces separate words. A number also
// matches the model id. Names starting with the query come first.
json SearchModels(const json& params)
{
	std::string query = params.value("query", std::string());
	for (char& c : query)
		c = c == '*' ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	std::vector<std::string> words;
	std::istringstream stream(query);
	for (std::string word; stream >> word;)
		words.push_back(word);

	const std::string wantedType = params.value("type", std::string());
	const int limit = std::clamp(params.value("limit", 25), 1, 200);
	const bool numeric = words.size() == 1 && words[0].find_first_not_of("0123456789") == std::string::npos;

	std::vector<const catalog::Entry*> first, rest;
	for (const catalog::Entry& entry : catalog::All())
	{
		bool match = true;
		for (const std::string& word : words)
			if (entry.lower.find(word) == std::string::npos)
			{
				match = false;
				break;
			}
		if (numeric && std::to_string(entry.id) == words[0])
			match = true;
		if (!match)
			continue;
		if (!wantedType.empty())
		{
			void* info = game::model::Info(entry.id);
			if (!info || wantedType != TypeName(game::model::Type(info)))
				continue;
		}
		const bool leading = !words.empty() && entry.lower.compare(0, words[0].size(), words[0]) == 0;
		(leading ? first : rest).push_back(&entry);
	}
	first.insert(first.end(), rest.begin(), rest.end());

	json list = json::array();
	for (const catalog::Entry* entry : first)
	{
		if (static_cast<int>(list.size()) >= limit)
			break;
		list.push_back(Brief(*entry));
	}
	return { { "total", first.size() }, { "returned", list.size() }, { "models", list } };
}

json GetModelInfo(const json& params)
{
	if (!params.contains("model"))
		throw CommandError("bad_params", "missing 'model' (an id or a name)");
	const int model = cmd::ResolveModel(params["model"]);
	void* info = game::model::Info(model);
	const int type = game::model::Type(info);

	if (params.value("load", false) && type != game::kModelVehicle && type != game::kModelPed)
		game::streaming::Load(model);

	json out = {
		{ "model", model },
		{ "type", TypeName(type) },
		{ "can_be_object", type != game::kModelVehicle && type != game::kModelPed && game::streaming::Exists(model) },
		{ "in_archives", game::streaming::Exists(model) },
		{ "loaded", game::streaming::IsLoaded(model) },
		{ "draw_distance", cmd::Round(game::model::DrawDistance(info), 1) },
		{ "bounds", BoundsOf(model) },
	};
	if (const catalog::Entry* entry = catalog::ById(model))
	{
		out["name"] = entry->name;
		out["txd"] = entry->txd;
		out["ide_section"] = entry->section;
		out["ide_file"] = entry->file;
	}
	else
		out["name"] = "";
	return out;
}

}

void RegisterModelCommands()
{
	dispatcher::Register("search_models", Phase::Tick, SearchModels);
	dispatcher::Register("get_model_info", Phase::Tick, GetModelInfo);
}
