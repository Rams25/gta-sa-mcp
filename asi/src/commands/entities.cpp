#include "entities.hpp"

#include "../editor/scene.hpp"

#include <algorithm>

namespace cmd
{

std::vector<Found> FindEntities(const game::Vec3& centre, float radius, const game::RayFilter& types, int model, bool renderedOnly)
{
	std::vector<Found> found;
	for (const game::PoolInfo& info : game::kPools)
	{
		const bool wanted =
			info.type == game::kBuilding ? types.buildings :
			info.type == game::kVehicle ? types.vehicles :
			info.type == game::kPed ? types.peds :
			info.type == game::kObject ? types.objects : types.dummies;
		const game::Pool* pool = wanted ? game::GetPool(info) : nullptr;
		if (!pool)
			continue;

		for (int slot = 0; slot < pool->size; ++slot)
		{
			if (pool->flags[slot] & 0x80)
				continue;
			void* entity = pool->objects + static_cast<std::size_t>(slot) * info.stride;
			if (model >= 0 && game::entity::Model(entity) != model)
				continue;
			if (renderedOnly && !game::entity::RwObject(entity))
				continue;
			const float distance = (game::entity::Position(entity) - centre).Length();
			if (distance <= radius)
				found.push_back({ entity, distance });
		}
	}
	std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.distance < b.distance; });
	return found;
}

}

namespace
{

using game::Vec3;

json GetNearbyEntities(const json& params)
{
	Vec3 centre = game::entity::Position(game::PlayerPed());
	const bool aroundCamera = params.value("around", std::string("player")) == "camera";
	if (aroundCamera)
		centre = game::camera::Read().position;
	cmd::OptionalVec3(params, "position", centre);

	const float radius = params.value("radius", 50.0f);
	const int limit = std::clamp(params.value("limit", 50), 1, 500);
	const game::RayFilter all { true, true, true, true, false };
	const game::RayFilter types = cmd::ParseFilter(params, "types", all);
	const int model = params.contains("model") && !params["model"].is_null() ? cmd::ResolveModel(params["model"]) : -1;

	const std::vector<cmd::Found> found = cmd::FindEntities(centre, radius, types, model, params.value("rendered_only", false));

	json list = json::array();
	void* player = game::PlayerPed();
	for (const cmd::Found& item : found)
	{
		if (static_cast<int>(list.size()) >= limit)
			break;
		json entry = cmd::Describe(item.entity);
		entry["distance"] = cmd::Round(item.distance, 2);
		if (item.entity == player)
			entry["is_player"] = true;
		list.push_back(std::move(entry));
	}
	return {
		{ "centre", cmd::ToJson(centre) },
		{ "radius", radius },
		{ "total", found.size() },
		{ "returned", list.size() },
		{ "entities", list },
	};
}

json GetEntity(const json& params)
{
	void* entity = cmd::RequireEntity(params);
	json out = cmd::Describe(entity);

	out["heading"] = cmd::Round(game::Deg(game::entity::Heading(entity)), 2);
	out["visible"] = game::entity::IsVisible(entity);
	out["collidable"] = game::entity::UsesCollision(entity);
	out["rendered"] = game::entity::RwObject(entity) != nullptr;
	out["interior"] = game::entity::Area(entity);
	out["lod"] = cmd::Describe(game::entity::Lod(entity));
	out["bounds"] = cmd::DescribeBounds(entity);
	out["selected"] = out.contains("object_id") && out["object_id"] == editor::Selected();

	const game::camera::View view = game::camera::Read();
	out["distance_to_camera"] = cmd::Round((game::entity::Position(entity) - view.position).Length(), 2);
	out["screen_rect"] = cmd::ScreenRect(view, entity);

	const game::EntityType type = game::entity::Type(entity);
	if (type == game::kPed)
		out["health"] = cmd::Round(game::Field<float>(entity, 0x540), 1);
	if (type == game::kVehicle)
		out["health"] = cmd::Round(game::Field<float>(entity, 0x4C0), 1);
	if (type == game::kPed || type == game::kVehicle || type == game::kObject)
		out["velocity"] = cmd::ToJson(game::Field<Vec3>(entity, 0x44) * 50.0f, 2);
	return out;
}

json GetEntityBounds(const json& params)
{
	void* entity = cmd::RequireEntity(params);
	json bounds = cmd::DescribeBounds(entity);
	if (bounds.is_null())
		throw CommandError("no_bounds", "this entity's model has no collision model, so no bounding box");

	json out = cmd::Describe(entity);
	out["bounds"] = std::move(bounds);
	out["screen_rect"] = cmd::ScreenRect(game::camera::Read(), entity);
	return out;
}

}

void RegisterEntityCommands()
{
	dispatcher::Register("get_nearby_entities", Phase::Tick, GetNearbyEntities);
	dispatcher::Register("get_entity", Phase::Tick, GetEntity);
	dispatcher::Register("get_entity_bounds", Phase::Tick, GetEntityBounds);
}
