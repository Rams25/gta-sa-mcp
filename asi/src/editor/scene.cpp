#include "scene.hpp"

#include "../core/log.hpp"
#include "../game/catalog.hpp"

#include <windows.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>

namespace editor
{

namespace
{

using game::Vec3;

enum class Kind { Created, Deleted, Moved };

struct Step
{
	Kind kind;
	int id;
	Vec3 positionBefore, rotationBefore, positionAfter, rotationAfter; // Moved only
	int group; // steps of one user action (a scene load) share a non-zero group
};

std::map<int, Object> g_objects; // deleted ones included, with a null entity
std::vector<Step> g_undo, g_redo;
int g_nextId = 1;
int g_nextGroup = 1;
int g_selected = 0;
bool g_dirty = false;

void Spawn(Object& object)
{
	void* info = game::model::Info(object.model);
	if (!info)
		throw CommandError("unknown_model", "model " + std::to_string(object.model) + " does not exist");
	const int type = game::model::Type(info);
	if (type == game::kModelVehicle || type == game::kModelPed)
		throw CommandError("bad_model", "model " + std::to_string(object.model) + " is a vehicle or a ped, not an object");
	if (!game::streaming::Exists(object.model))
		throw CommandError("model_not_available", "model " + std::to_string(object.model) + " has no file in the game archives");

	object.entity = game::object::Create(object.model, object.position, object.rotation);
	if (!object.entity)
		throw CommandError("spawn_failed", "the game could not create an object with model " + std::to_string(object.model)
			+ " (model failed to load, or the object pool is full)");
}

void Despawn(Object& object)
{
	if (object.entity)
		game::object::Destroy(object.entity);
	object.entity = nullptr;
	if (g_selected == object.id)
		g_selected = 0;
}

Object& Live(int id)
{
	const auto found = g_objects.find(id);
	if (found == g_objects.end() || !found->second.entity)
		throw CommandError("unknown_object", "no object with object_id " + std::to_string(id) + " (see list_objects)");
	return found->second;
}

void Record(const Step& step)
{
	g_undo.push_back(step);
	g_redo.clear();
	g_dirty = true;
}

Object& CreateInternal(int model, const Vec3& position, const Vec3& rotation, const std::string& name, int group)
{
	Object object;
	object.id = g_nextId;
	object.model = model;
	object.position = position;
	object.rotation = rotation;
	object.name = name;
	Spawn(object);

	++g_nextId;
	Object& stored = g_objects[object.id] = object;
	Record({ Kind::Created, stored.id, {}, {}, {}, {}, group });
	return stored;
}

void DeleteInternal(int id, int group)
{
	Object& object = Live(id);
	Despawn(object);
	Record({ Kind::Deleted, id, {}, {}, {}, {}, group });
}

// Applies one step backwards or forwards.
void Apply(const Step& step, bool forward)
{
	Object& object = g_objects.at(step.id);
	const bool exists = (step.kind == Kind::Created) == forward;
	switch (step.kind)
	{
	case Kind::Created:
	case Kind::Deleted:
		if (exists && !object.entity)
			Spawn(object);
		else if (!exists)
			Despawn(object);
		break;
	case Kind::Moved:
		object.position = forward ? step.positionAfter : step.positionBefore;
		object.rotation = forward ? step.rotationAfter : step.rotationBefore;
		if (object.entity)
			game::object::SetTransform(object.entity, object.position, object.rotation);
		break;
	}
}

const char* KindName(Kind kind, bool forward)
{
	switch (kind)
	{
	case Kind::Created: return forward ? "create" : "delete";
	case Kind::Deleted: return forward ? "delete" : "create";
	default: return "transform";
	}
}

json Replay(std::vector<Step>& from, std::vector<Step>& to, bool forward)
{
	if (from.empty())
		return nullptr;

	json changes = json::array();
	const int group = from.back().group;
	do
	{
		const Step step = from.back();
		Apply(step, forward);
		from.pop_back();
		to.push_back(step);
		changes.push_back({ { "object_id", step.id }, { "effect", KindName(step.kind, forward) } });
	} while (group != 0 && !from.empty() && from.back().group == group);

	g_dirty = true;
	return changes;
}

// ---- Files -----------------------------------------------------------------------------------

std::wstring ScenesDirectory()
{
	const std::wstring root = PluginDirectory() + L"gta-sa-mcp";
	CreateDirectoryW(root.c_str(), nullptr);
	const std::wstring scenes = root + L"\\scenes";
	CreateDirectoryW(scenes.c_str(), nullptr);
	return scenes + L"\\";
}

// A scene name is a file name: letters, digits, '-' and '_' only.
std::string CleanName(const std::string& name)
{
	std::string clean;
	for (const char c : name)
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
			clean += c;
	if (clean.size() > 64)
		clean.resize(64);
	return clean.empty() ? "scene" : clean;
}

json Triple(const Vec3& v)
{
	return json::array({ v.x, v.y, v.z });
}

Vec3 FromTriple(const json& value)
{
	return { value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>() };
}

std::string Label(const Object& object)
{
	const std::string modelName = game::catalog::NameOf(object.model);
	if (object.name.empty())
		return modelName;
	return modelName.empty() ? object.name : modelName + " - " + object.name;
}

// SA-MP / open.mp: CreateObject(model, x, y, z, rx, ry, rz) uses the same angles as we do.
std::string ExportPawn(const std::vector<const Object*>& objects)
{
	std::string out;
	char line[256];
	for (const Object* o : objects)
	{
		std::snprintf(line, sizeof(line), "CreateObject(%d, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f); // %s\n",
			o->model, o->position.x, o->position.y, o->position.z, o->rotation.x, o->rotation.y, o->rotation.z, Label(*o).c_str());
		out += line;
	}
	return out;
}

// Map placement file: `inst` lines store the conjugate of the object's rotation quaternion.
std::string ExportIpl(const std::vector<const Object*>& objects)
{
	std::string out = "inst\n";
	char line[256];
	for (const Object* o : objects)
	{
		game::Matrix m {};
		game::SetRotation(m, o->rotation);
		// Rotation matrix with the basis vectors as columns.
		const float m00 = m.right.x, m01 = m.forward.x, m02 = m.up.x;
		const float m10 = m.right.y, m11 = m.forward.y, m12 = m.up.y;
		const float m20 = m.right.z, m21 = m.forward.z, m22 = m.up.z;
		float x, y, z, w;
		const float trace = m00 + m11 + m22;
		if (trace > 0.0f)
		{
			const float s = std::sqrt(trace + 1.0f) * 2.0f;
			w = 0.25f * s; x = (m21 - m12) / s; y = (m02 - m20) / s; z = (m10 - m01) / s;
		}
		else if (m00 > m11 && m00 > m22)
		{
			const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
			w = (m21 - m12) / s; x = 0.25f * s; y = (m01 + m10) / s; z = (m02 + m20) / s;
		}
		else if (m11 > m22)
		{
			const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
			w = (m02 - m20) / s; x = (m01 + m10) / s; y = 0.25f * s; z = (m12 + m21) / s;
		}
		else
		{
			const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
			w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = 0.25f * s;
		}
		std::string modelName = game::catalog::NameOf(o->model);
		if (modelName.empty())
			modelName = "dummy";
		std::snprintf(line, sizeof(line), "%d, %s, 0, %.4f, %.4f, %.4f, %.6f, %.6f, %.6f, %.6f, -1\n",
			o->model, modelName.c_str(), o->position.x, o->position.y, o->position.z, -x, -y, -z, w);
		out += line;
	}
	return out + "end\n";
}

bool WriteText(const std::wstring& path, const std::string& content)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file << content;
	return static_cast<bool>(file);
}

}

// ---- Objects ---------------------------------------------------------------------------------

const Object& Create(int model, const Vec3& position, const Vec3& rotation, const std::string& name)
{
	return CreateInternal(model, position, rotation, name, 0);
}

const Object& Transform(int id, const Vec3& position, const Vec3& rotation)
{
	Object& object = Live(id);
	Step step { Kind::Moved, id, object.position, object.rotation, position, rotation, 0 };
	object.position = position;
	object.rotation = rotation;
	game::object::SetTransform(object.entity, position, rotation);
	Record(step);
	return object;
}

void Delete(int id)
{
	DeleteInternal(id, 0);
}

const Object& Clone(int id, const Vec3& position, const Vec3& rotation)
{
	const Object source = Live(id);
	return CreateInternal(source.model, position, rotation, source.name, 0);
}

const Object* Find(int id)
{
	const auto found = g_objects.find(id);
	return found != g_objects.end() && found->second.entity ? &found->second : nullptr;
}

const Object& Require(int id)
{
	return Live(id);
}

const Object* FindByEntity(void* entity)
{
	if (entity)
		for (const auto& entry : g_objects)
			if (entry.second.entity == entity)
				return &entry.second;
	return nullptr;
}

std::vector<const Object*> List()
{
	std::vector<const Object*> live;
	for (const auto& entry : g_objects)
		if (entry.second.entity)
			live.push_back(&entry.second);
	return live;
}

int Selected()
{
	return g_selected;
}

void Select(int id)
{
	if (id != 0)
		Live(id);
	g_selected = id;
}

// ---- History ---------------------------------------------------------------------------------

json Undo()
{
	return Replay(g_undo, g_redo, false);
}

json Redo()
{
	return Replay(g_redo, g_undo, true);
}

json HistoryState()
{
	return {
		{ "undo_steps", g_undo.size() },
		{ "redo_steps", g_redo.size() },
		{ "unsaved_changes", g_dirty },
		{ "object_count", List().size() },
	};
}

// ---- Files -----------------------------------------------------------------------------------

json Save(const std::string& name, const std::string& format)
{
	if (!format.empty() && format != "json" && format != "pawn" && format != "ipl")
		throw CommandError("bad_params", "format must be \"json\", \"pawn\" or \"ipl\"");

	const std::string clean = CleanName(name);
	const std::vector<const Object*> objects = List();

	json list = json::array();
	for (const Object* o : objects)
		list.push_back({
			{ "id", o->id },
			{ "model", o->model },
			{ "model_name", game::catalog::NameOf(o->model) },
			{ "position", Triple(o->position) },
			{ "rotation", Triple(o->rotation) },
			{ "name", o->name },
		});
	const json document = {
		{ "format", "gta-sa-mcp-scene" },
		{ "version", 1 },
		{ "name", clean },
		{ "saved_at", static_cast<long long>(std::time(nullptr)) },
		{ "objects", list },
	};

	const std::wstring base = ScenesDirectory() + FromUtf8(clean);
	json files = json::array();
	if (!WriteText(base + L".json", document.dump(2, ' ', false, json::error_handler_t::replace)))
		throw CommandError("io_error", "could not write " + ToUtf8(base) + ".json");
	files.push_back(ToUtf8(base) + ".json");

	if (format == "pawn" || format == "ipl")
	{
		const std::wstring path = base + (format == "pawn" ? L".pwn" : L".ipl");
		if (!WriteText(path, format == "pawn" ? ExportPawn(objects) : ExportIpl(objects)))
			throw CommandError("io_error", "could not write " + ToUtf8(path));
		files.push_back(ToUtf8(path));
	}

	g_dirty = false;
	Log("Scene '" + clean + "' saved: " + std::to_string(objects.size()) + " objects.");
	return { { "name", clean }, { "object_count", objects.size() }, { "files", files } };
}

json Load(const std::string& name, bool replace)
{
	const std::string clean = CleanName(name);
	const std::wstring path = ScenesDirectory() + FromUtf8(clean) + L".json";
	std::ifstream file(path, std::ios::binary);
	if (!file)
		throw CommandError("not_found", "no saved scene named '" + clean + "' (" + ToUtf8(path) + ")");
	const json document = json::parse(file, nullptr, false);
	if (!document.is_object() || !document.contains("objects") || !document["objects"].is_array())
		throw CommandError("bad_file", ToUtf8(path) + " is not a scene file");

	const int group = g_nextGroup++;
	int removed = 0;
	if (replace)
		for (const Object* object : List())
		{
			DeleteInternal(object->id, group);
			++removed;
		}

	json created = json::array();
	json failed = json::array();
	for (const json& item : document["objects"])
	{
		try
		{
			const Object& object = CreateInternal(item.at("model").get<int>(), FromTriple(item.at("position")),
				FromTriple(item.at("rotation")), item.value("name", std::string()), group);
			created.push_back(object.id);
		}
		catch (const std::exception& e)
		{
			failed.push_back({ { "entry", item }, { "error", e.what() } });
		}
	}
	return { { "name", clean }, { "created_object_ids", created }, { "removed", removed }, { "failed", failed } };
}

}
