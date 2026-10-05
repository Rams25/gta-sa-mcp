// The part of gta_sa.exe 1.0 US this plugin uses: addresses, layouts and thin typed wrappers.
// Addresses and names follow the gta-reversed project. Game thread only, unless stated otherwise.
#pragma once

#include <cmath>
#include <cstdint>

namespace game
{

// ---- Math ------------------------------------------------------------------------------------

struct Vec3
{
	float x = 0.0f, y = 0.0f, z = 0.0f;

	Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
	Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
	Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
	float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
	Vec3 Cross(const Vec3& o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
	float Length() const { return std::sqrt(Dot(*this)); }
	Vec3 Normalized() const
	{
		const float length = Length();
		return length > 1e-6f ? *this * (1.0f / length) : Vec3 { 0.0f, 1.0f, 0.0f };
	}
};

// CMatrix / RwMatrix: three basis vectors and a position, 16 bytes apart.
struct Matrix
{
	Vec3 right;
	std::uint32_t flags;
	Vec3 forward;
	std::uint32_t pad1;
	Vec3 up;
	std::uint32_t pad2;
	Vec3 pos;
	std::uint32_t pad3;

	Vec3 Transform(const Vec3& local) const { return pos + right * local.x + forward * local.y + up * local.z; }
};

constexpr float kPi = 3.14159265358979f;
inline float Deg(float radians) { return radians * 180.0f / kPi; }
inline float Rad(float degrees) { return degrees * kPi / 180.0f; }

// Euler angles in degrees, in the game's own convention (CMatrix::SetRotate, the one SA-MP's
// CreateObject and MTA's default object rotation use): Z, then X, then Y.
void SetRotation(Matrix& matrix, const Vec3& degrees);
Vec3 GetRotation(const Matrix& matrix);

// ---- Raw access ------------------------------------------------------------------------------

template <typename T>
inline T& At(std::uintptr_t address)
{
	return *reinterpret_cast<T*>(address);
}

template <typename T>
inline T& Field(void* object, std::size_t offset)
{
	return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

template <typename T>
inline const T& Field(const void* object, std::size_t offset)
{
	return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(object) + offset);
}

inline void* Virtual(void* object, std::size_t index)
{
	return (*reinterpret_cast<void***>(object))[index];
}

// ---- Game state (readable from any thread) -----------------------------------------------------

// gGameState: 7 = main menu, 8 = loading, 9 = in game.
inline int GameState() { return At<int>(0xC8D4C0); }
inline bool RwInitialized() { return At<bool>(0xC920E8); }
inline int ScreenWidth() { return At<int>(0xC17044); }  // RsGlobal.maximumWidth
inline int ScreenHeight() { return At<int>(0xC17048); } // RsGlobal.maximumHeight
inline void* PlayerPed() { return reinterpret_cast<void* (__cdecl*)(int)>(0x56E210)(-1); }
inline void* PlayerVehicle() { return reinterpret_cast<void* (__cdecl*)(int, bool)>(0x56E0D0)(-1, false); }

// True once the menus can be drawn (a frame hook runs).
inline bool RenderReady() { return RwInitialized() && GameState() >= 7; }
// True once the world is simulated and the player exists.
bool InGame();

// ---- Entities --------------------------------------------------------------------------------

enum EntityType : std::uint8_t
{
	kNothing = 0,
	kBuilding = 1,
	kVehicle = 2,
	kPed = 3,
	kObject = 4,
	kDummy = 5,
};

namespace entity
{
inline Matrix* GetMatrix(void* e) { return Field<Matrix*>(e, 0x14); }
inline void* RwObject(void* e) { return Field<void*>(e, 0x18); }
inline std::uint32_t Flags(void* e) { return Field<std::uint32_t>(e, 0x1C); }
inline int Model(void* e) { return Field<std::uint16_t>(e, 0x22); }
inline int Area(void* e) { return Field<std::uint8_t>(e, 0x2F); }
inline void* Lod(void* e) { return Field<void*>(e, 0x30); }
inline EntityType Type(void* e) { return static_cast<EntityType>(Field<std::uint8_t>(e, 0x36) & 7); }
inline bool UsesCollision(void* e) { return (Flags(e) & 0x1) != 0; }
inline bool IsVisible(void* e) { return (Flags(e) & 0x80) != 0; }

// Buildings keep a position + heading until something needs their full matrix.
Vec3 Position(void* e);
float Heading(void* e); // radians
// The entity's transform, built from position + heading when it has no matrix.
Matrix Transform(void* e);
}

// CPools: fixed arrays with one flag byte per slot.
struct Pool
{
	std::uint8_t* objects;
	std::uint8_t* flags; // bit 7: free slot, low bits: reuse counter
	int size;
};

struct PoolInfo
{
	EntityType type;
	const char* name;
	std::uintptr_t pointer; // address of the CPool*
	std::size_t stride;
};

// Peds, vehicles, buildings, objects, dummies.
extern const PoolInfo kPools[5];
const PoolInfo* PoolOf(EntityType type);
const PoolInfo* PoolByName(const char* name);
inline Pool* GetPool(const PoolInfo& info) { return At<Pool*>(info.pointer); }

// (slot << 8) | reuse counter: stays unique when a slot is recycled. -1 if not in a pool.
int HandleOf(void* e);
void* FromHandle(const PoolInfo& info, int handle);

// ---- World -----------------------------------------------------------------------------------

struct ColPoint
{
	Vec3 point;
	float pad0;
	Vec3 normal;
	float pad1;
	std::uint8_t surfaceA, pieceA, lightA, surfaceB, pieceB, lightB;
	std::uint8_t pad2[2];
	float depth;
};

struct RayFilter
{
	bool buildings = true, vehicles = true, peds = true, objects = true, dummies = false;
};

namespace world
{
inline void Add(void* e) { reinterpret_cast<void (__cdecl*)(void*)>(0x563220)(e); }
inline void Remove(void* e) { reinterpret_cast<void (__cdecl*)(void*)>(0x563280)(e); }

bool Raycast(const Vec3& from, const Vec3& to, const RayFilter& filter, ColPoint& hit, void*& entity);
// Highest ground at or below `from`. False when there is none (or its collision is not loaded).
bool GroundZ(const Vec3& from, float& z, void** entity = nullptr);
// First entity overlapping the sphere, or null.
void* SphereTest(const Vec3& centre, float radius, void* ignore, const RayFilter& filter);
}

// ---- Streaming -------------------------------------------------------------------------------

namespace streaming
{
constexpr int kModelCount = 20000;

// False when the model has no file in the game archives.
bool Exists(int model);
bool IsLoaded(int model);
// Loads the model now and keeps it in memory. False if it could not be loaded.
bool Load(int model);
// Loads the map and its collision around a point, blocking.
void LoadScene(const Vec3& position);
}

// ---- Models ----------------------------------------------------------------------------------

enum ModelType : std::uint8_t
{
	kModelAtomic = 1,
	kModelTime = 3,
	kModelWeapon = 4,
	kModelClump = 5,
	kModelVehicle = 6,
	kModelPed = 7,
	kModelLod = 8,
};

struct Bounds
{
	Vec3 min, max, centre;
	float radius = 0.0f;
};

namespace model
{
inline void* Info(int model)
{
	return model >= 0 && model < streaming::kModelCount ? At<void*[streaming::kModelCount]>(0xA9B0C8)[model] : nullptr;
}
int Type(void* info);
inline float DrawDistance(void* info) { return Field<float>(info, 0x18); }
// Bounding box and sphere of the collision model, in model space. False if the model has none.
bool GetBounds(int model, Bounds& out);
}

// ---- Objects ---------------------------------------------------------------------------------

namespace object
{
// Creates a static world object. Null if the model cannot be an object or cannot be loaded.
void* Create(int model, const Vec3& position, const Vec3& rotationDegrees);
void SetTransform(void* object, const Vec3& position, const Vec3& rotationDegrees);
void Destroy(void* object);
}

}
