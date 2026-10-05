#include "common.hpp"

#include "../editor/scene.hpp"

namespace
{

using game::Vec3;
namespace camera = game::camera;

const game::RayFilter kSolid { true, true, true, true, false };

json Raycast(const json& params)
{
	const Vec3 from = cmd::RequireVec3(params, "from");
	Vec3 to;
	if (!cmd::OptionalVec3(params, "to", to))
	{
		const Vec3 direction = cmd::RequireVec3(params, "direction").Normalized();
		to = from + direction * params.value("distance", 300.0f);
	}

	game::ColPoint point {};
	void* entity = nullptr;
	const bool hit = game::world::Raycast(from, to, cmd::ParseFilter(params, "types", kSolid), point, entity);
	json out = cmd::DescribeHit(hit, from, point, entity);
	out["from"] = cmd::ToJson(from);
	out["to"] = cmd::ToJson(to);
	return out;
}

// Pixel coordinates, or 0..1 fractions of the screen with "normalized": true.
void ScreenPoint(const json& params, const camera::View& view, float& x, float& y)
{
	x = params.at("x").get<float>();
	y = params.at("y").get<float>();
	if (params.value("normalized", false))
	{
		x *= static_cast<float>(view.width);
		y *= static_cast<float>(view.height);
	}
}

json ScreenToWorld(const json& params)
{
	const camera::View view = camera::Read();
	if (!view.valid)
		throw CommandError("no_camera", "the game camera is not set up yet");

	float x, y;
	ScreenPoint(params, view, x, y);
	Vec3 origin, direction;
	camera::Ray(view, x, y, origin, direction);

	const float distance = params.value("max_distance", 1000.0f);
	game::ColPoint point {};
	void* entity = nullptr;
	const bool hit = game::world::Raycast(origin, origin + direction * distance, cmd::ParseFilter(params, "types", kSolid), point, entity);

	json out = cmd::DescribeHit(hit, origin, point, entity);
	out["screen"] = json::array({ cmd::Round(x, 1), cmd::Round(y, 1) });
	out["ray_origin"] = cmd::ToJson(origin);
	out["ray_direction"] = cmd::ToJson(direction, 5);
	return out;
}

json ProjectPoint(const camera::View& view, const Vec3& world)
{
	float x, y, depth;
	if (!camera::Project(view, world, x, y, depth))
		return { { "world", cmd::ToJson(world) }, { "in_front", false }, { "on_screen", false } };
	const bool onScreen = x >= 0.0f && y >= 0.0f && x < static_cast<float>(view.width) && y < static_cast<float>(view.height);
	return {
		{ "world", cmd::ToJson(world) },
		{ "in_front", true },
		{ "on_screen", onScreen },
		{ "x", cmd::Round(x, 1) },
		{ "y", cmd::Round(y, 1) },
		{ "depth", cmd::Round(depth, 2) },
	};
}

json WorldToScreen(const json& params)
{
	const camera::View view = camera::Read();
	if (!view.valid)
		throw CommandError("no_camera", "the game camera is not set up yet");

	if (params.contains("positions") && params["positions"].is_array())
	{
		json points = json::array();
		for (const json& item : params["positions"])
			points.push_back(ProjectPoint(view, cmd::ParseVec3(item, "positions[]")));
		return { { "resolution", json::array({ view.width, view.height }) }, { "points", points } };
	}
	json out = ProjectPoint(view, cmd::RequireVec3(params, "position"));
	out["resolution"] = json::array({ view.width, view.height });
	return out;
}

json GetGroundZ(const json& params)
{
	Vec3 from;
	if (!cmd::OptionalVec3(params, "position", from))
		from = { params.at("x").get<float>(), params.at("y").get<float>(), params.value("z", 1000.0f) };

	float z = 0.0f;
	void* ground = nullptr;
	if (!game::world::GroundZ(from, z, &ground))
		return {
			{ "found", false },
			{ "hint", "no ground below this point, or its collision is not loaded: collision only exists around the player (teleport there first)" },
		};
	return { { "found", true }, { "z", cmd::Round(z) }, { "position", cmd::ToJson({ from.x, from.y, z }) }, { "entity", cmd::Describe(ground) } };
}

json IsPositionFree(const json& params)
{
	const Vec3 position = cmd::RequireVec3(params, "position");
	const float radius = params.value("radius", 1.0f);

	void* ignore = nullptr;
	if (params.contains("ignore_object_id") && !params["ignore_object_id"].is_null())
		ignore = editor::Require(params["ignore_object_id"].get<int>()).entity;
	else if (params.contains("ignore_entity") && params["ignore_entity"].is_string())
		ignore = cmd::ResolveRef(params["ignore_entity"].get<std::string>());

	void* blocker = game::world::SphereTest(position, radius, ignore, cmd::ParseFilter(params, "types", kSolid));
	return {
		{ "free", blocker == nullptr },
		{ "position", cmd::ToJson(position) },
		{ "radius", radius },
		{ "blocked_by", cmd::Describe(blocker) },
	};
}

// Time of day, weather and HUD: what a screenshot looks like.
json SetWorld(const json& params)
{
	if (params.contains("hour"))
	{
		game::At<std::uint8_t>(0xB70153) = static_cast<std::uint8_t>(params["hour"].get<int>() % 24); // CClock hours
		game::At<std::uint8_t>(0xB70152) = static_cast<std::uint8_t>(params.value("minute", 0) % 60);
	}
	if (params.contains("freeze_time"))
		// CClock::ms_nMillisecondsPerGameMinute: a huge value stops the clock.
		game::At<std::uint32_t>(0xB7015C) = params["freeze_time"].get<bool>() ? 0x7FFFFFFFu : 1000u;
	if (params.contains("weather"))
		reinterpret_cast<void (__cdecl*)(short)>(0x72A4F0)(static_cast<short>(params["weather"].get<int>())); // CWeather::ForceWeatherNow
	if (params.contains("hud"))
		game::ShowHud(params["hud"].get<bool>());

	return {
		{ "hour", game::At<std::uint8_t>(0xB70153) },
		{ "minute", game::At<std::uint8_t>(0xB70152) },
		{ "weather", game::At<short>(0xC81320) }, // CWeather::OldWeatherType
		{ "hud", game::HudShown() },
	};
}

}

void RegisterWorldCommands()
{
	dispatcher::Register("raycast", Phase::Tick, Raycast);
	dispatcher::Register("screen_to_world", Phase::Tick, ScreenToWorld);
	dispatcher::Register("world_to_screen", Phase::Tick, WorldToScreen);
	dispatcher::Register("get_ground_z", Phase::Tick, GetGroundZ);
	dispatcher::Register("is_position_free", Phase::Tick, IsPositionFree);
	dispatcher::Register("set_world", Phase::Tick, SetWorld, true);
}
