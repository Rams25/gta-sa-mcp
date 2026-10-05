#include "common.hpp"

namespace
{

using game::Vec3;
namespace camera = game::camera;

json GetCamera(const json&)
{
	const camera::View view = camera::Read();
	if (!view.valid)
		throw CommandError("no_camera", "the game camera is not set up yet");
	json out = cmd::DescribeView(view);
	if (camera::Scripted().active)
		out["target"] = cmd::ToJson(camera::Scripted().target);
	return out;
}

// What a set_camera/look_at answers: the camera as requested (the game applies it next frame).
json Placed()
{
	const camera::Shot& shot = camera::Scripted();
	const Vec3 front = (shot.target - shot.position).Normalized();
	return {
		{ "position", cmd::ToJson(shot.position) },
		{ "target", cmd::ToJson(shot.target) },
		{ "heading", cmd::Round(game::Deg(std::atan2(-front.x, front.y)), 2) },
		{ "pitch", cmd::Round(game::Deg(std::asin(front.z)), 2) },
		{ "fov", cmd::Round(shot.fov, 2) },
		{ "mode", "fixed" },
	};
}

void MaybeLoadScene(const json& params, const Vec3& position)
{
	if (params.value("load_scene", false))
		game::streaming::LoadScene(position);
}

json SetCamera(const json& params)
{
	if (params.value("reset", false))
	{
		camera::Restore();
		return { { "mode", "game" } };
	}

	const camera::View view = camera::Read();
	const camera::Shot& current = camera::Scripted();

	Vec3 position = current.active ? current.position : view.position;
	cmd::OptionalVec3(params, "position", position);

	// Aim: an explicit target, else heading/pitch, else keep looking the same way.
	Vec3 target;
	if (!cmd::OptionalVec3(params, "target", target))
	{
		Vec3 front = current.active ? (current.target - current.position).Normalized() : view.front;
		if (params.contains("heading") || params.contains("pitch"))
		{
			const float heading = params.value("heading", game::Deg(std::atan2(-front.x, front.y)));
			const float pitch = params.value("pitch", game::Deg(std::asin(front.z)));
			front = camera::Direction(heading, pitch);
		}
		target = position + front * 10.0f;
	}

	const float fov = params.value("fov", current.active ? current.fov : 70.0f);
	MaybeLoadScene(params, position);
	camera::Set(position, target, fov);
	return Placed();
}

json LookAt(const json& params)
{
	Vec3 target;
	if (params.contains("entity") || params.contains("object_id"))
	{
		void* entity = cmd::RequireEntity(params);
		game::Bounds bounds;
		const game::Matrix transform = game::entity::Transform(entity);
		target = game::model::GetBounds(game::entity::Model(entity), bounds) ? transform.Transform(bounds.centre) : transform.pos;
	}
	else
		target = cmd::RequireVec3(params, "target");

	const camera::View view = camera::Read();
	const camera::Shot& current = camera::Scripted();
	Vec3 position = current.active ? current.position : view.position;
	if (!cmd::OptionalVec3(params, "position", position) && params.contains("distance"))
	{
		// Orbit: place the camera `distance` away from the target, seen from heading/pitch.
		const float distance = params["distance"].get<float>();
		const float heading = params.value("heading", 0.0f);
		const float pitch = params.value("pitch", -30.0f);
		position = target - camera::Direction(heading, pitch) * distance;
	}

	const float fov = params.value("fov", current.active ? current.fov : 70.0f);
	MaybeLoadScene(params, position);
	camera::Set(position, target, fov);
	return Placed();
}

}

void RegisterCameraCommands()
{
	dispatcher::Register("get_camera", Phase::Tick, GetCamera);
	dispatcher::Register("set_camera", Phase::Tick, SetCamera, true);
	dispatcher::Register("look_at", Phase::Tick, LookAt, true);
}
