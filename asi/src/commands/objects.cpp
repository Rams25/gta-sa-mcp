// The editing commands: objects we place, their history and their files.
#include "common.hpp"

#include "../editor/scene.hpp"

namespace
{

using game::Vec3;

json DescribeObject(const editor::Object& object)
{
	json out = cmd::Describe(object.entity);
	// The values as asked for, not as read back from the matrix (angles would come back rewrapped).
	out["position"] = cmd::ToJson(object.position);
	out["rotation"] = cmd::ToJson(object.rotation, 2);
	out["selected"] = editor::Selected() == object.id;
	out["bounds"] = cmd::DescribeBounds(object.entity);
	return out;
}

int ObjectId(const json& params)
{
	if (params.contains("object_id") && params["object_id"].is_number_integer())
		return params["object_id"].get<int>();
	if (editor::Selected() != 0)
		return editor::Selected();
	throw CommandError("bad_params", "missing 'object_id' (and no object is selected)");
}

// Puts the bottom of the object's bounding box on the ground below (or just above) `position`.
bool SnapToGround(int model, Vec3& position, void* self)
{
	game::Bounds bounds;
	const float base = game::model::GetBounds(model, bounds) ? bounds.min.z : 0.0f;

	// Our own object must not count as the ground it stands on.
	game::ColPoint point {};
	void* hit = nullptr;
	const Vec3 from { position.x, position.y, position.z + 3.0f };
	const Vec3 to { position.x, position.y, position.z - 500.0f };
	game::RayFilter filter { true, false, false, true, false };
	if (self)
		game::world::Remove(self);
	const bool found = game::world::Raycast(from, to, filter, point, hit);
	if (self)
		game::world::Add(self);
	if (!found)
		return false;
	position.z = point.point.z - base;
	return true;
}

json CreateObject(const json& params)
{
	if (!params.contains("model"))
		throw CommandError("bad_params", "missing 'model' (an id or a name)");
	const int model = cmd::ResolveModel(params["model"]);
	Vec3 position = cmd::RequireVec3(params, "position");
	Vec3 rotation;
	cmd::OptionalVec3(params, "rotation", rotation);

	bool snapped = false;
	if (params.value("snap_to_ground", false))
	{
		game::streaming::Load(model); // bounds of some models only exist once loaded
		snapped = SnapToGround(model, position, nullptr);
	}

	const editor::Object& object = editor::Create(model, position, rotation, params.value("name", std::string()));
	if (params.value("select", true))
		editor::Select(object.id);

	json out = DescribeObject(object);
	if (params.contains("snap_to_ground"))
		out["snapped_to_ground"] = snapped;
	return out;
}

json MoveObject(const json& params)
{
	const editor::Object& object = editor::Require(ObjectId(params));
	Vec3 position = object.position;
	Vec3 offset;
	if (cmd::OptionalVec3(params, "position", position))
		;
	else if (cmd::OptionalVec3(params, "offset", offset))
	{
		if (params.value("local", false))
		{
			// Along the object's own axes.
			game::Matrix m {};
			game::SetRotation(m, object.rotation);
			position = position + m.right * offset.x + m.forward * offset.y + m.up * offset.z;
		}
		else
			position = position + offset;
	}
	else if (!params.value("snap_to_ground", false))
		throw CommandError("bad_params", "give 'position' (absolute) or 'offset' (relative)");

	bool snapped = false;
	if (params.value("snap_to_ground", false))
		snapped = SnapToGround(object.model, position, object.entity);

	json out = DescribeObject(editor::Transform(object.id, position, object.rotation));
	if (params.contains("snap_to_ground"))
		out["snapped_to_ground"] = snapped;
	return out;
}

json RotateObject(const json& params)
{
	const editor::Object& object = editor::Require(ObjectId(params));
	Vec3 rotation = object.rotation;
	Vec3 delta;
	if (cmd::OptionalVec3(params, "rotation", rotation))
		;
	else if (cmd::OptionalVec3(params, "delta", delta))
		rotation = rotation + delta;
	else
		throw CommandError("bad_params", "give 'rotation' (absolute, degrees) or 'delta' (relative, degrees)");
	return DescribeObject(editor::Transform(object.id, object.position, rotation));
}

json DeleteObject(const json& params)
{
	const int id = ObjectId(params);
	editor::Delete(id);
	json out = editor::HistoryState();
	out["deleted_object_id"] = id;
	return out;
}

json CloneObject(const json& params)
{
	const editor::Object& source = editor::Require(ObjectId(params));
	Vec3 position = source.position;
	Vec3 offset { 0.0f, 0.0f, 0.0f };
	if (!cmd::OptionalVec3(params, "position", position) && cmd::OptionalVec3(params, "offset", offset))
		position = position + offset;
	Vec3 rotation = source.rotation;
	cmd::OptionalVec3(params, "rotation", rotation);

	const editor::Object& clone = editor::Clone(source.id, position, rotation);
	if (params.value("select", true))
		editor::Select(clone.id);
	json out = DescribeObject(clone);
	out["cloned_from"] = source.id;
	return out;
}

json SelectObject(const json& params)
{
	const int id = params.contains("object_id") && params["object_id"].is_number_integer() ? params["object_id"].get<int>() : 0;
	editor::Select(id);
	if (id == 0)
		return { { "selected", nullptr } };
	return { { "selected", DescribeObject(editor::Require(id)) } };
}

json ListObjects(const json&)
{
	json list = json::array();
	for (const editor::Object* object : editor::List())
	{
		json entry = cmd::Describe(object->entity);
		entry["position"] = cmd::ToJson(object->position);
		entry["rotation"] = cmd::ToJson(object->rotation, 2);
		list.push_back(std::move(entry));
	}
	json out = editor::HistoryState();
	out["selected_object_id"] = editor::Selected() ? json(editor::Selected()) : json(nullptr);
	out["objects"] = std::move(list);
	return out;
}

json History(json changes, const char* nothing)
{
	json out = editor::HistoryState();
	if (changes.is_null())
	{
		out["done"] = false;
		out["message"] = nothing;
	}
	else
	{
		out["done"] = true;
		out["changes"] = std::move(changes);
	}
	return out;
}

json Undo(const json&)
{
	return History(editor::Undo(), "nothing to undo");
}

json Redo(const json&)
{
	return History(editor::Redo(), "nothing to redo");
}

json SaveChanges(const json& params)
{
	return editor::Save(params.value("name", std::string("scene")), params.value("format", std::string()));
}

json LoadChanges(const json& params)
{
	return editor::Load(params.value("name", std::string("scene")), params.value("replace", true));
}

}

void RegisterObjectCommands()
{
	dispatcher::Register("create_object", Phase::Tick, CreateObject, true);
	dispatcher::Register("move_object", Phase::Tick, MoveObject, true);
	dispatcher::Register("rotate_object", Phase::Tick, RotateObject, true);
	dispatcher::Register("delete_object", Phase::Tick, DeleteObject, true);
	dispatcher::Register("clone_object", Phase::Tick, CloneObject, true);
	dispatcher::Register("select_object", Phase::Tick, SelectObject);
	dispatcher::Register("list_objects", Phase::Tick, ListObjects);
	dispatcher::Register("undo", Phase::Tick, Undo, true);
	dispatcher::Register("redo", Phase::Tick, Redo, true);
	dispatcher::Register("save_changes", Phase::Tick, SaveChanges);
	dispatcher::Register("load_changes", Phase::Tick, LoadChanges, true);
}
