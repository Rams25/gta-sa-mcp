#include "sdk.hpp"

#include <cstring>

namespace game
{

// ---- Math ------------------------------------------------------------------------------------

void SetRotation(Matrix& matrix, const Vec3& degrees)
{
	const float sx = std::sin(Rad(degrees.x)), cx = std::cos(Rad(degrees.x));
	const float sy = std::sin(Rad(degrees.y)), cy = std::cos(Rad(degrees.y));
	const float sz = std::sin(Rad(degrees.z)), cz = std::cos(Rad(degrees.z));

	matrix.right = { cz * cy - sz * sx * sy, cz * sx * sy + sz * cy, -(sy * cx) };
	matrix.forward = { -(sz * cx), cz * cx, sx };
	matrix.up = { cz * sy + sz * sx * cy, sz * sy - cz * sx * cy, cy * cx };
}

Vec3 GetRotation(const Matrix& m)
{
	float sx = m.forward.z;
	if (sx > 1.0f) sx = 1.0f;
	if (sx < -1.0f) sx = -1.0f;

	Vec3 degrees;
	degrees.x = Deg(std::asin(sx));
	if (std::fabs(sx) < 0.9999f)
	{
		degrees.z = Deg(std::atan2(-m.forward.x, m.forward.y));
		degrees.y = Deg(std::atan2(-m.right.z, m.up.z));
	}
	else
	{
		// Pointing straight up or down: Y and Z turn around the same axis, keep it all in Z.
		degrees.y = 0.0f;
		degrees.z = Deg(std::atan2(m.right.y, m.right.x));
	}
	return degrees;
}

// ---- Game state ------------------------------------------------------------------------------

bool InGame()
{
	return RwInitialized() && GameState() == 9 && PlayerPed() != nullptr;
}

// ---- Entities --------------------------------------------------------------------------------

namespace entity
{

Vec3 Position(void* e)
{
	const Matrix* matrix = GetMatrix(e);
	return matrix ? matrix->pos : Field<Vec3>(e, 0x4);
}

float Heading(void* e)
{
	const Matrix* matrix = GetMatrix(e);
	return matrix ? std::atan2(-matrix->forward.x, matrix->forward.y) : Field<float>(e, 0x10);
}

Matrix Transform(void* e)
{
	Matrix out {};
	if (const Matrix* matrix = GetMatrix(e))
	{
		out = *matrix;
		return out;
	}
	SetRotation(out, { 0.0f, 0.0f, Deg(Field<float>(e, 0x10)) });
	out.pos = Field<Vec3>(e, 0x4);
	return out;
}

}

const PoolInfo kPools[5] = {
	{ kPed, "ped", 0xB74490, 0x7C4 },
	{ kVehicle, "vehicle", 0xB74494, 0xA18 },
	{ kBuilding, "building", 0xB74498, 0x38 },
	{ kObject, "object", 0xB7449C, 0x19C },
	{ kDummy, "dummy", 0xB744A0, 0x38 },
};

const PoolInfo* PoolOf(EntityType type)
{
	for (const PoolInfo& info : kPools)
		if (info.type == type)
			return &info;
	return nullptr;
}

const PoolInfo* PoolByName(const char* name)
{
	for (const PoolInfo& info : kPools)
		if (std::strcmp(info.name, name) == 0)
			return &info;
	return nullptr;
}

int HandleOf(void* e)
{
	const PoolInfo* info = e ? PoolOf(entity::Type(e)) : nullptr;
	const Pool* pool = info ? GetPool(*info) : nullptr;
	if (!pool)
		return -1;
	const std::ptrdiff_t offset = static_cast<std::uint8_t*>(e) - pool->objects;
	const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(info->stride);
	if (offset < 0 || offset % stride != 0)
		return -1;
	const int slot = static_cast<int>(offset / stride);
	if (slot >= pool->size)
		return -1;
	return (slot << 8) | pool->flags[slot];
}

void* FromHandle(const PoolInfo& info, int handle)
{
	const Pool* pool = GetPool(info);
	const int slot = handle >> 8;
	if (!pool || handle < 0 || slot >= pool->size)
		return nullptr;
	const std::uint8_t flags = pool->flags[slot];
	if ((flags & 0x80) || flags != (handle & 0xFF))
		return nullptr;
	return pool->objects + static_cast<std::size_t>(slot) * info.stride;
}

// ---- World -----------------------------------------------------------------------------------

namespace world
{

bool Raycast(const Vec3& from, const Vec3& to, const RayFilter& filter, ColPoint& hit, void*& entity)
{
	// CWorld::ProcessLineOfSight; the last three: see-through, camera-ignore and shoot-through checks.
	using Fn = bool (__cdecl*)(const Vec3*, const Vec3*, ColPoint*, void**, bool, bool, bool, bool, bool, bool, bool, bool);
	entity = nullptr;
	return reinterpret_cast<Fn>(0x56BA00)(&from, &to, &hit, &entity,
		filter.buildings, filter.vehicles, filter.peds, filter.objects, filter.dummies, false, false, false);
}

bool GroundZ(const Vec3& from, float& z, void** entity)
{
	// CWorld::FindGroundZFor3DCoord
	using Fn = float (__cdecl*)(Vec3, bool*, void**);
	bool found = false;
	void* ground = nullptr;
	z = reinterpret_cast<Fn>(0x5696C0)(from, &found, &ground);
	if (entity)
		*entity = ground;
	return found;
}

void* SphereTest(const Vec3& centre, float radius, void* ignore, const RayFilter& filter)
{
	// CWorld::TestSphereAgainstWorld; the last flag is the camera-ignore check.
	using Fn = void* (__cdecl*)(Vec3, float, void*, bool, bool, bool, bool, bool, bool);
	return reinterpret_cast<Fn>(0x569E20)(centre, radius, ignore,
		filter.buildings, filter.vehicles, filter.peds, filter.objects, filter.dummies, false);
}

}

// ---- Streaming -------------------------------------------------------------------------------

namespace streaming
{

namespace
{
// CStreamingInfo, 20 bytes per model.
std::uint8_t* InfoOf(int model) { return reinterpret_cast<std::uint8_t*>(0x8E4CC0) + model * 0x14; }
}

bool Exists(int model)
{
	return model >= 0 && model < kModelCount && Field<std::uint32_t>(InfoOf(model), 0xC) != 0;
}

bool IsLoaded(int model)
{
	return model >= 0 && model < kModelCount && Field<std::uint8_t>(InfoOf(model), 0x10) == 1;
}

bool Load(int model)
{
	if (IsLoaded(model))
		return true;
	if (!Exists(model))
		return false;
	// Mission-required + keep in memory: the game never unloads it under an object we own.
	reinterpret_cast<void (__cdecl*)(int, int)>(0x4087E0)(model, 0x4 | 0x8);
	reinterpret_cast<void (__cdecl*)(bool)>(0x40EA10)(false);
	return IsLoaded(model);
}

void LoadScene(const Vec3& position)
{
	reinterpret_cast<void (__cdecl*)(const Vec3*)>(0x40ED80)(&position); // collision
	reinterpret_cast<void (__cdecl*)(const Vec3*)>(0x40EB70)(&position); // models
}

}

// ---- Models ----------------------------------------------------------------------------------

namespace model
{

int Type(void* info)
{
	return reinterpret_cast<std::uint8_t (__thiscall*)(void*)>(Virtual(info, 4))(info);
}

bool GetBounds(int model, Bounds& out)
{
	void* info = Info(model);
	void* col = info ? Field<void*>(info, 0x14) : nullptr;
	if (!col)
		return false;
	out.min = Field<Vec3>(col, 0x0);
	out.max = Field<Vec3>(col, 0xC);
	out.centre = Field<Vec3>(col, 0x18);
	out.radius = Field<float>(col, 0x24);
	return true;
}

}

// ---- Objects ---------------------------------------------------------------------------------

namespace object
{

namespace
{

void Write(void* object, const Vec3& position, const Vec3& rotationDegrees)
{
	if (Matrix* matrix = entity::GetMatrix(object))
	{
		SetRotation(*matrix, rotationDegrees);
		matrix->pos = position;
	}
	else
	{
		Field<Vec3>(object, 0x4) = position;
		Field<float>(object, 0x10) = Rad(rotationDegrees.z);
	}
	reinterpret_cast<void (__thiscall*)(void*)>(0x446F90)(object); // CEntity::UpdateRwMatrix
	reinterpret_cast<void (__thiscall*)(void*)>(0x532B00)(object); // CEntity::UpdateRwFrame
}

}

void* Create(int model, const Vec3& position, const Vec3& rotationDegrees)
{
	void* info = model::Info(model);
	if (!info)
		return nullptr;
	const int type = model::Type(info);
	if (type == kModelVehicle || type == kModelPed)
		return nullptr;
	if (!streaming::Load(model))
		return nullptr;

	void* object = reinterpret_cast<void* (__cdecl*)(int, bool)>(0x5A1F60)(model, false); // CObject::Create
	if (!object)
		return nullptr;

	Field<std::uint8_t>(object, 0x13C) = 2; // OBJECT_MISSION: never recycled as a temporary object
	// Frozen in place: no gravity, no reaction to hits, infinite mass.
	std::uint32_t& physical = Field<std::uint32_t>(object, 0x40);
	physical = (physical & ~0x2u) | 0x4u | 0x10u | 0x20u | 0x40u;
	Field<float>(object, 0x8C) = 99999.0f;
	Field<float>(object, 0x90) = 99999.0f;
	reinterpret_cast<void (__thiscall*)(void*, bool)>(Virtual(object, 4))(object, true); // SetIsStatic

	Write(object, position, rotationDegrees);
	world::Add(object);
	return object;
}

void SetTransform(void* object, const Vec3& position, const Vec3& rotationDegrees)
{
	// Out of the world sectors and back in: they are indexed by position.
	world::Remove(object);
	Write(object, position, rotationDegrees);
	world::Add(object);
}

void Destroy(void* object)
{
	world::Remove(object);
	reinterpret_cast<void (__thiscall*)(void*, int)>(Virtual(object, 0))(object, 1); // deleting destructor
}

}

}
