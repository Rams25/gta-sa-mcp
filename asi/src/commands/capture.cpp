// Vision: the frame the game just drew, alone or with everything known about what it shows.
#include "entities.hpp"

#include "../core/config.hpp"
#include "../editor/scene.hpp"
#include "../render/capture.hpp"

#include <algorithm>

namespace
{

using game::Vec3;
namespace camera = game::camera;

// {"image": {base64, mime_type, width, height}}; the MCP server turns it into an image block.
json Screenshot(const json& params)
{
	const Config& config = GetConfig();
	const std::string format = params.value("format", std::string("jpeg"));
	if (format != "jpeg" && format != "png")
		throw CommandError("bad_params", "format must be \"jpeg\" or \"png\"");

	capture::Image image;
	std::string error;
	if (!capture::Grab(image, error))
		throw CommandError("capture_failed", error);
	const int fullWidth = image.width, fullHeight = image.height;
	capture::Downscale(image, params.value("max_width", config.maxWidth));

	const std::vector<std::uint8_t> file = capture::Encode(image, format, std::clamp(params.value("quality", config.jpegQuality), 1, 100));
	if (file.empty())
		throw CommandError("capture_failed", "could not encode the screenshot");

	return {
		{ "base64", capture::Base64(file) },
		{ "mime_type", format == "png" ? "image/png" : "image/jpeg" },
		{ "width", image.width },
		{ "height", image.height },
		// Screen coordinates everywhere else are in game pixels: multiply image pixels by this.
		{ "image_to_screen_scale", cmd::Round(static_cast<float>(fullWidth) / static_cast<float>(image.width), 4) },
		{ "screen_resolution", json::array({ fullWidth, fullHeight }) },
	};
}

json TakeScreenshot(const json& params)
{
	return { { "image", Screenshot(params) } };
}

// Is something solid between the camera and the entity? A ray to the centre of its bounding
// sphere must reach the entity itself, or stop no earlier than the sphere's surface.
bool Occluded(const camera::View& view, void* entity, const Vec3& centre, float radius)
{
	game::ColPoint point {};
	void* hit = nullptr;
	const game::RayFilter solid { true, true, false, true, false };
	if (!game::world::Raycast(view.position, centre, solid, point, hit) || hit == entity)
		return false;
	const float reached = (point.point - view.position).Length();
	return reached < (centre - view.position).Length() - radius;
}

json VisibleEntities(const camera::View& view, const json& params)
{
	const float radius = params.value("radius", 150.0f);
	const int limit = std::clamp(params.value("max_entities", 40), 0, 300);
	const bool checkOcclusion = params.value("occlusion", true);
	const game::RayFilter all { true, true, true, true, false };
	const game::RayFilter types = cmd::ParseFilter(params, "types", all);

	json list = json::array();
	int inView = 0;
	for (const cmd::Found& item : cmd::FindEntities(view.position, radius, types, -1, true))
	{
		void* entity = item.entity;
		if (!game::entity::IsVisible(entity))
			continue;

		game::Bounds bounds;
		const bool hasBounds = game::model::GetBounds(game::entity::Model(entity), bounds);
		const game::Matrix transform = game::entity::Transform(entity);
		const Vec3 centre = hasBounds ? transform.Transform(bounds.centre) : transform.pos;
		const float size = hasBounds ? bounds.radius : 1.0f;

		// In the view frustum, counting the bounding sphere's size on screen.
		float x = 0.0f, y = 0.0f, depth = 0.0f;
		const Vec3 d = centre - view.position;
		if (d.Dot(view.front) < -size)
			continue;
		if (camera::Project(view, centre, x, y, depth))
		{
			const float marginX = size / (depth * view.tanX) * 0.5f * static_cast<float>(view.width);
			const float marginY = size / (depth * view.tanY) * 0.5f * static_cast<float>(view.height);
			if (x < -marginX || x > static_cast<float>(view.width) + marginX || y < -marginY || y > static_cast<float>(view.height) + marginY)
				continue;
		}
		else if (d.Length() > size)
			continue;

		++inView;
		if (static_cast<int>(list.size()) >= limit)
			continue;

		json entry = cmd::Describe(entity);
		entry["distance"] = cmd::Round(item.distance, 2);
		entry["screen"] = depth >= 0.05f ? json::array({ cmd::Round(x, 0), cmd::Round(y, 0) }) : json(nullptr);
		entry["screen_rect"] = cmd::ScreenRect(view, entity);
		if (hasBounds)
		{
			entry["bounds_min"] = cmd::ToJson(bounds.min, 2);
			entry["bounds_max"] = cmd::ToJson(bounds.max, 2);
		}
		if (checkOcclusion)
			entry["occluded"] = Occluded(view, entity, centre, size);
		list.push_back(std::move(entry));
	}
	return { { "in_view", inView }, { "returned", list.size() }, { "radius", radius }, { "list", list } };
}

// What the camera is aimed at, the ground under it, and a coarse grid of depth probes: enough to
// place something "where I am looking" without a second round trip.
json Probes(const camera::View& view, const json& params)
{
	const game::RayFilter solid { true, true, true, true, false };
	const float reach = params.value("probe_distance", 500.0f);

	auto probe = [&](float fx, float fy) {
		Vec3 origin, direction;
		camera::Ray(view, fx * static_cast<float>(view.width), fy * static_cast<float>(view.height), origin, direction);
		game::ColPoint point {};
		void* hit = nullptr;
		const bool found = game::world::Raycast(origin, origin + direction * reach, solid, point, hit);
		json out = cmd::DescribeHit(found, origin, point, hit);
		out["screen"] = json::array({ cmd::Round(fx * static_cast<float>(view.width), 0), cmd::Round(fy * static_cast<float>(view.height), 0) });
		return out;
	};

	json grid = json::array();
	const int cells = std::clamp(params.value("probe_grid", 3), 0, 8);
	for (int row = 0; row < cells; ++row)
		for (int column = 0; column < cells; ++column)
		{
			json cell = probe((static_cast<float>(column) + 0.5f) / static_cast<float>(cells), (static_cast<float>(row) + 0.5f) / static_cast<float>(cells));
			cell.erase("normal");
			if (cell.contains("entity") && cell["entity"].is_object())
				cell["entity"] = cell["entity"]["entity"]; // the reference is enough here
			grid.push_back(std::move(cell));
		}

	json out = { { "centre", probe(0.5f, 0.5f) }, { "grid", grid } };
	float groundZ = 0.0f;
	if (game::world::GroundZ(view.position, groundZ))
	{
		out["ground_z_below_camera"] = cmd::Round(groundZ);
		out["camera_height_above_ground"] = cmd::Round(view.position.z - groundZ);
	}
	return out;
}

json CaptureScene(const json& params)
{
	json out;
	if (params.value("screenshot", true))
		out["image"] = Screenshot(params);

	// In the menus or while loading there is a picture but no world behind it.
	const camera::View view = camera::Read();
	if (!game::InGame() || !view.valid)
	{
		out["in_game"] = false;
		out["note"] = "the game is not in play: only the screenshot is available";
		return out;
	}
	out["in_game"] = true;
	out["camera"] = cmd::DescribeView(view);
	out["entities"] = VisibleEntities(view, params);
	out["collision"] = Probes(view, params);

	json selected = nullptr;
	if (const editor::Object* object = editor::Find(editor::Selected()))
	{
		selected = cmd::Describe(object->entity);
		selected["position"] = cmd::ToJson(object->position);
		selected["rotation"] = cmd::ToJson(object->rotation, 2);
		selected["bounds"] = cmd::DescribeBounds(object->entity);
		selected["screen_rect"] = cmd::ScreenRect(view, object->entity);
	}
	out["selected_object"] = std::move(selected);
	out["editor"] = editor::HistoryState();

	void* player = game::PlayerPed();
	out["player"] = { { "position", cmd::ToJson(game::entity::Position(player)) }, { "distance_to_camera", cmd::Round((game::entity::Position(player) - view.position).Length(), 2) } };
	return out;
}

}

void RegisterCaptureCommands()
{
	dispatcher::Register("take_screenshot", Phase::Frame, TakeScreenshot);
	dispatcher::Register("capture_scene", Phase::Frame, CaptureScene);
}
