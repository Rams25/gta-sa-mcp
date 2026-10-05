#include "common.hpp"

#include "../editor/scene.hpp"
#include "../game/catalog.hpp"

#include <cstdlib>

namespace cmd
{

double Round(float value, int decimals)
{
	double scale = 1.0;
	for (int i = 0; i < decimals; ++i)
		scale *= 10.0;
	return std::round(static_cast<double>(value) * scale) / scale;
}

json ToJson(const Vec3& v, int decimals)
{
	return json::array({ Round(v.x, decimals), Round(v.y, decimals), Round(v.z, decimals) });
}

Vec3 ParseVec3(const json& value, const char* what)
{
	if (value.is_array() && value.size() == 3 && value[0].is_number() && value[1].is_number() && value[2].is_number())
		return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
	if (value.is_object() && value.contains("x") && value.contains("y") && value.contains("z"))
		return { value["x"].get<float>(), value["y"].get<float>(), value["z"].get<float>() };
	throw CommandError("bad_params", std::string(what) + " must be [x, y, z]");
}

Vec3 RequireVec3(const json& params, const char* key)
{
	if (!params.contains(key))
		throw CommandError("bad_params", std::string("missing '") + key + "'");
	return ParseVec3(params[key], key);
}

bool OptionalVec3(const json& params, const char* key, Vec3& out)
{
	if (!params.contains(key) || params[key].is_null())
		return false;
	out = ParseVec3(params[key], key);
	return true;
}

std::string RefOf(void* entity)
{
	const game::PoolInfo* pool = entity ? game::PoolOf(game::entity::Type(entity)) : nullptr;
	const int handle = pool ? game::HandleOf(entity) : -1;
	return handle < 0 ? std::string() : std::string(pool->name) + ":" + std::to_string(handle);
}

void* ResolveRef(const std::string& ref)
{
	const std::size_t colon = ref.find(':');
	const game::PoolInfo* pool = colon == std::string::npos ? nullptr : game::PoolByName(ref.substr(0, colon).c_str());
	if (!pool)
		throw CommandError("bad_params", "'" + ref + "' is not an entity reference (expected e.g. \"building:118272\")");
	void* entity = game::FromHandle(*pool, std::atoi(ref.c_str() + colon + 1));
	if (!entity)
		throw CommandError("unknown_entity", "entity " + ref + " no longer exists (the map streamed it out?)");
	return entity;
}

void* RequireEntity(const json& params)
{
	if (params.contains("object_id") && !params["object_id"].is_null())
		return editor::Require(params["object_id"].get<int>()).entity;
	if (params.contains("entity") && params["entity"].is_string())
		return ResolveRef(params["entity"].get<std::string>());
	throw CommandError("bad_params", "give 'entity' (a reference such as \"building:118272\") or 'object_id'");
}

int ResolveModel(const json& value)
{
	int id = -1;
	if (value.is_number_integer())
		id = value.get<int>();
	else if (value.is_string())
	{
		const std::string text = value.get<std::string>();
		if (const game::catalog::Entry* entry = game::catalog::ByName(text))
			id = entry->id;
		else if (!text.empty() && text.find_first_not_of("0123456789") == std::string::npos)
			id = std::atoi(text.c_str());
		else
			throw CommandError("unknown_model", "no model named '" + text + "' (try search_models)");
	}
	else
		throw CommandError("bad_params", "'model' must be a model id or a model name");

	if (!game::model::Info(id))
		throw CommandError("unknown_model", "model " + std::to_string(id) + " does not exist");
	return id;
}

game::RayFilter ParseFilter(const json& params, const char* key, const game::RayFilter& fallback)
{
	if (!params.contains(key) || !params[key].is_array())
		return fallback;
	game::RayFilter filter { false, false, false, false, false };
	for (const json& item : params[key])
	{
		const std::string type = item.get<std::string>();
		if (type == "building") filter.buildings = true;
		else if (type == "vehicle") filter.vehicles = true;
		else if (type == "ped") filter.peds = true;
		else if (type == "object") filter.objects = true;
		else if (type == "dummy") filter.dummies = true;
		else throw CommandError("bad_params", "unknown entity type '" + type + "' (building, vehicle, ped, object, dummy)");
	}
	return filter;
}

json Describe(void* entity)
{
	if (!entity)
		return nullptr;
	const game::PoolInfo* pool = game::PoolOf(game::entity::Type(entity));
	const int model = game::entity::Model(entity);
	const game::Matrix transform = game::entity::Transform(entity);

	json out = {
		{ "entity", RefOf(entity) },
		{ "type", pool ? pool->name : "unknown" },
		{ "model", model },
		{ "model_name", game::catalog::NameOf(model) },
		{ "position", ToJson(transform.pos) },
		{ "rotation", ToJson(game::GetRotation(transform), 2) },
	};
	if (const editor::Object* object = editor::FindByEntity(entity))
	{
		out["object_id"] = object->id;
		if (!object->name.empty())
			out["name"] = object->name;
	}
	return out;
}

json DescribeBounds(void* entity)
{
	game::Bounds bounds;
	if (!entity || !game::model::GetBounds(game::entity::Model(entity), bounds))
		return nullptr;

	const game::Matrix transform = game::entity::Transform(entity);
	json corners = json::array();
	Vec3 low { 1e9f, 1e9f, 1e9f }, high { -1e9f, -1e9f, -1e9f };
	for (int i = 0; i < 8; ++i)
	{
		const Vec3 corner = transform.Transform({
			(i & 1) ? bounds.max.x : bounds.min.x,
			(i & 2) ? bounds.max.y : bounds.min.y,
			(i & 4) ? bounds.max.z : bounds.min.z });
		corners.push_back(ToJson(corner));
		low = { std::fmin(low.x, corner.x), std::fmin(low.y, corner.y), std::fmin(low.z, corner.z) };
		high = { std::fmax(high.x, corner.x), std::fmax(high.y, corner.y), std::fmax(high.z, corner.z) };
	}
	return {
		{ "local_min", ToJson(bounds.min) },
		{ "local_max", ToJson(bounds.max) },
		{ "size", ToJson(bounds.max - bounds.min) },
		{ "radius", Round(bounds.radius) },
		{ "world_centre", ToJson(transform.Transform(bounds.centre)) },
		{ "world_min", ToJson(low) },
		{ "world_max", ToJson(high) },
		{ "world_corners", corners },
	};
}

json ScreenRect(const game::camera::View& view, void* entity)
{
	game::Bounds bounds;
	if (!view.valid || !entity || !game::model::GetBounds(game::entity::Model(entity), bounds))
		return nullptr;

	const game::Matrix transform = game::entity::Transform(entity);
	float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
	int inFront = 0;
	for (int i = 0; i < 8; ++i)
	{
		const Vec3 corner = transform.Transform({
			(i & 1) ? bounds.max.x : bounds.min.x,
			(i & 2) ? bounds.max.y : bounds.min.y,
			(i & 4) ? bounds.max.z : bounds.min.z });
		float x, y, depth;
		if (!game::camera::Project(view, corner, x, y, depth))
			continue;
		++inFront;
		left = std::fmin(left, x);
		right = std::fmax(right, x);
		top = std::fmin(top, y);
		bottom = std::fmax(bottom, y);
	}
	if (inFront == 0)
		return nullptr;
	return {
		{ "left", Round(left, 0) },
		{ "top", Round(top, 0) },
		{ "right", Round(right, 0) },
		{ "bottom", Round(bottom, 0) },
		// Some corners are behind the camera: the rectangle only covers the visible ones.
		{ "partial", inFront < 8 },
	};
}

json DescribeView(const game::camera::View& view)
{
	if (!view.valid)
		return nullptr;
	const game::camera::Shot& shot = game::camera::Scripted();
	return {
		{ "position", ToJson(view.position) },
		{ "forward", ToJson(view.front, 4) },
		{ "up", ToJson(view.up, 4) },
		{ "heading", Round(view.HeadingDegrees(), 2) },
		{ "pitch", Round(view.PitchDegrees(), 2) },
		{ "fov", Round(shot.active ? shot.fov : game::At<float>(0x8D5038), 2) }, // CDraw::ms_fFOV
		{ "fov_horizontal", Round(view.FovXDegrees(), 2) },
		{ "fov_vertical", Round(view.FovYDegrees(), 2) },
		{ "near_clip", Round(view.nearClip) },
		{ "far_clip", Round(view.farClip, 1) },
		{ "resolution", json::array({ view.width, view.height }) },
		{ "mode", shot.active ? "fixed" : "game" },
	};
}

json DescribeHit(bool hit, const Vec3& from, const game::ColPoint& point, void* entity)
{
	if (!hit)
		return { { "hit", false } };
	return {
		{ "hit", true },
		{ "position", ToJson(point.point) },
		{ "normal", ToJson(point.normal, 4) },
		{ "distance", Round((point.point - from).Length()) },
		{ "surface", point.surfaceB },
		{ "entity", Describe(entity) },
	};
}

}
