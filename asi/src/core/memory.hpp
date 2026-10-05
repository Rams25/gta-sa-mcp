// Patching helpers for gta_sa.exe (1.0 US, the only version every mod targets).
#pragma once

#include <cstddef>
#include <cstdint>

namespace mem
{

// Writes over read-only code/data. False if the page could not be unprotected.
bool Write(std::uintptr_t address, const void* data, std::size_t size);

template <typename T>
bool Set(std::uintptr_t address, T value)
{
	return Write(address, &value, sizeof(value));
}

bool Nop(std::uintptr_t address, std::size_t size);

// Redirects the `call rel32` at `site` to `target`. Returns the previous target, so the hook can
// chain to it (another mod may already have hooked the same site), or 0 if `site` is not a call.
std::uintptr_t HookCall(std::uintptr_t site, void* target);

// Replaces an import of the game executable. Returns the previous pointer, or null if not imported.
void* HookImport(const char* dll, const char* function, void* replacement);

// Replaces entry `index` of a COM object's vtable in place. Returns the previous entry, or null if
// the entry already was `replacement` (or could not be written).
void* HookVtable(void* object, std::size_t index, void* replacement);

}
