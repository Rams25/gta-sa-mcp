#include "assets.hpp"

#include "../core/log.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace game::assets
{

namespace
{

struct Override
{
	int originalTxd = -1;
	int txdSlot = -1;        // our own texture dictionary slot, -1 if none
	std::string col;         // collision file to keep applied, empty if none
	void* colData = nullptr; // CColModel::m_pColData as we left it
};

std::map<int, Override> g_overrides;
unsigned g_tick = 0;

// CStreamingInfo of a streaming id (models first, then texture dictionaries from 20000).
std::uint8_t* StreamingInfo(int id) { return reinterpret_cast<std::uint8_t*>(0x8E4CC0) + id * 0x14; }

std::int16_t& ModelTxd(void* info) { return Field<std::int16_t>(info, 0xA); }
void*& ModelCol(void* info) { return Field<void*>(info, 0x14); }

// CTxdStore::ms_pTxdPool entry: dictionary, reference count, parent slot.
std::uint8_t* TxdDef(int slot)
{
	const Pool* pool = At<Pool*>(0xC8800C);
	if (!pool || slot < 0 || slot >= pool->size || (pool->flags[slot] & 0x80))
		return nullptr;
	return pool->objects + slot * 0xC;
}

bool ReadFile(const std::string& path, std::vector<std::uint8_t>& out)
{
	FILE* file = nullptr;
	if (fopen_s(&file, path.c_str(), "rb") != 0 || !file)
		return false;
	std::fseek(file, 0, SEEK_END);
	const long size = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);
	out.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
	const bool ok = !out.empty() && std::fread(out.data(), 1, out.size(), file) == out.size();
	std::fclose(file);
	return ok;
}

template <typename Function>
void ForEachEntity(int model, Function&& function)
{
	for (const PoolInfo& info : kPools)
	{
		const Pool* pool = GetPool(info);
		if (!pool)
			continue;
		for (int slot = 0; slot < pool->size; ++slot)
		{
			if (pool->flags[slot] & 0x80)
				continue;
			void* e = pool->objects + static_cast<std::size_t>(slot) * info.stride;
			if (entity::Model(e) == model)
				function(e);
		}
	}
}

// Drops the 3D instances of every entity using the model; the renderer recreates them from
// whatever the model is by then.
int DropInstances(int model)
{
	int count = 0;
	ForEachEntity(model, [&](void* e) {
		if (!entity::RwObject(e))
			return;
		reinterpret_cast<void (__thiscall*)(void*)>(Virtual(e, 8))(e); // CEntity::DeleteRwObject
		++count;
	});
	return count;
}

void UnloadModel(int model)
{
	reinterpret_cast<void (__cdecl*)(int)>(0x4089A0)(model); // CStreaming::RemoveModel
}

bool LoadTextures(int model, void* info, const std::string& path, Override& entry, std::string& error)
{
	if (entry.txdSlot < 0)
	{
		char name[32];
		std::snprintf(name, sizeof(name), "mcp_%d", model);
		int slot = reinterpret_cast<int (__cdecl*)(const char*)>(0x731850)(name); // CTxdStore::FindTxdSlot
		if (slot < 0)
			slot = reinterpret_cast<int (__cdecl*)(const char*)>(0x731C80)(name); // CTxdStore::AddTxdSlot
		if (slot < 0 || !TxdDef(slot))
		{
			error = "no free texture dictionary slot";
			return false;
		}
		entry.txdSlot = slot;
		reinterpret_cast<void (__cdecl*)(int)>(0x731A00)(slot); // CTxdStore::AddRef: ours to keep
	}

	if (Field<void*>(TxdDef(entry.txdSlot), 0x0))
		reinterpret_cast<void (__cdecl*)(int)>(0x731E90)(entry.txdSlot); // CTxdStore::RemoveTxd
	if (!reinterpret_cast<bool (__cdecl*)(int, const char*)>(0x7320B0)(entry.txdSlot, path.c_str())) // CTxdStore::LoadTxd
	{
		error = "the game could not read " + path + " as a texture dictionary";
		return false;
	}
	ModelTxd(info) = static_cast<std::int16_t>(entry.txdSlot);
	return true;
}

bool LoadGeometry(int model, void* info, const std::string& path, std::string& error)
{
	// RwStreamOpen(file name, read)
	void* stream = reinterpret_cast<void* (__cdecl*)(int, int, const void*)>(0x7ECEF0)(2, 1, path.c_str());
	if (!stream)
	{
		error = "cannot open " + path;
		return false;
	}

	// Textures are looked up in the current dictionary while the clump is read.
	reinterpret_cast<void (__cdecl*)()>(0x7316A0)();                  // CTxdStore::PushCurrentTxd
	reinterpret_cast<void (__cdecl*)(int)>(0x7319C0)(ModelTxd(info)); // CTxdStore::SetCurrentTxd
	const bool loaded = reinterpret_cast<bool (__cdecl*)(void*, unsigned)>(0x5371F0)(stream, static_cast<unsigned>(model)); // CFileLoader::LoadAtomicFile
	reinterpret_cast<void (__cdecl*)()>(0x7316B0)();                  // CTxdStore::PopCurrentTxd
	reinterpret_cast<int (__cdecl*)(void*, void*)>(0x7ECE20)(stream, nullptr); // RwStreamClose

	if (!loaded || !Field<void*>(info, 0x1C))
	{
		error = "the game could not read " + path + " as a model";
		return false;
	}

	// Loaded and "required by the game": streaming neither reloads it from the archive nor
	// unloads it (models with that flag are not in its unload list).
	std::uint8_t* streaming = StreamingInfo(model);
	Field<std::int16_t>(streaming, 0x0) = -1;
	Field<std::int16_t>(streaming, 0x2) = -1;
	Field<std::uint8_t>(streaming, 0x6) = 0x2;
	Field<std::uint8_t>(streaming, 0x10) = 1;
	return true;
}

bool LoadCollision(void* info, const std::string& path, std::string& error)
{
	std::vector<std::uint8_t> file;
	if (!ReadFile(path, file) || file.size() < 32 + 40)
	{
		error = "cannot read " + path;
		return false;
	}
	// Header: "COL3", size of what follows, model name[22], model id.
	const bool v3 = std::memcmp(file.data(), "COL3", 4) == 0;
	const bool v2 = std::memcmp(file.data(), "COL2", 4) == 0;
	std::uint32_t size = 0;
	std::memcpy(&size, file.data() + 4, 4);
	if ((!v2 && !v3) || size < 24 || size + 8 > file.size())
	{
		error = path + " is not a COL2/COL3 collision file";
		return false;
	}
	void* col = ModelCol(info);
	if (!col)
	{
		error = "this model has no collision model to replace";
		return false;
	}

	char name[24] = {};
	std::memcpy(name, file.data() + 8, 22);
	reinterpret_cast<void (__thiscall*)(void*)>(0x40F9E0)(col); // CColModel::RemoveCollisionVolumes
	// CFileLoader::LoadCollisionModelVer3 / Ver2 (data after the header, its size, model, name)
	reinterpret_cast<void (__cdecl*)(std::uint8_t*, std::uint32_t, void*, const char*)>(v3 ? 0x537CE0 : 0x537EE0)(
		file.data() + 32, size - 24, col, name);
	return true;
}

// The world indexes entities by the area their collision covers: register them again.
void Reindex(int model)
{
	ForEachEntity(model, [](void* e) {
		world::Remove(e);
		world::Add(e);
	});
}

}

Result Replace(int model, const std::string& dff, const std::string& txd, const std::string& col, int transparent)
{
	Result result;
	void* info = model::Info(model);
	if (!info)
	{
		result.error = "model " + std::to_string(model) + " does not exist";
		return result;
	}
	const int type = model::Type(info);
	if (type != kModelAtomic && type != kModelTime && type != kModelLod)
	{
		result.error = "only static object models can be replaced";
		return result;
	}

	const bool known = g_overrides.count(model) != 0;
	Override& entry = g_overrides[model];
	if (!known)
		entry.originalTxd = ModelTxd(info);

	if (!dff.empty() || !txd.empty())
	{
		// Geometry and textures go together: the model holds references into its dictionary.
		result.entitiesRefreshed = DropInstances(model);
		const bool hadGeometry = Field<void*>(info, 0x1C) != nullptr;
		UnloadModel(model);

		if (!txd.empty() && !LoadTextures(model, info, txd, entry, result.error))
			return result;
		if (!dff.empty())
		{
			if (!LoadGeometry(model, info, dff, result.error))
				return result;
		}
		else if (hadGeometry)
			reinterpret_cast<void (__cdecl*)(int, int)>(0x4087E0)(model, 0x8); // CStreaming::RequestModel: the game's own geometry again
	}
	result.txdSlot = entry.txdSlot;

	if (!col.empty())
	{
		if (!LoadCollision(info, col, result.error))
			return result;
		entry.col = col;
		entry.colData = Field<void*>(ModelCol(info), 0x2C);
		Reindex(model);
	}

	if (transparent >= 0)
	{
		// CBaseModelInfo::bDrawLast, copied to each entity (CEntity::m_bDrawLast) when it is created.
		std::uint16_t& flags = Field<std::uint16_t>(info, 0x12);
		flags = static_cast<std::uint16_t>(transparent ? flags | 0x2 : flags & ~0x2);
		ForEachEntity(model, [&](void* e) {
			std::uint32_t& entityFlags = Field<std::uint32_t>(e, 0x1C);
			entityFlags = transparent ? entityFlags | 0x4000u : entityFlags & ~0x4000u;
		});
	}

	result.ok = true;
	Log("Model " + std::to_string(model) + " replaced from files.");
	return result;
}

bool Restore(int model)
{
	const auto found = g_overrides.find(model);
	void* info = model::Info(model);
	if (found == g_overrides.end() || !info)
		return false;

	DropInstances(model);
	UnloadModel(model);
	Field<std::uint8_t>(StreamingInfo(model), 0x6) = 0;
	ModelTxd(info) = static_cast<std::int16_t>(found->second.originalTxd);
	g_overrides.erase(found);
	return true;
}

void Tick()
{
	if (g_overrides.empty() || ++g_tick % 30 != 0)
		return;
	for (auto& entry : g_overrides)
	{
		Override& replaced = entry.second;
		void* info = model::Info(entry.first);
		void* col = info ? ModelCol(info) : nullptr;
		if (replaced.col.empty() || !col || Field<void*>(col, 0x2C) == replaced.colData)
			continue;
		std::string error;
		if (LoadCollision(info, replaced.col, error))
		{
			replaced.colData = Field<void*>(col, 0x2C);
			Reindex(entry.first);
		}
	}
}

}
