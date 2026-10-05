#include "memory.hpp"

#include <windows.h>

#include <cstring>

namespace mem
{

bool Write(std::uintptr_t address, const void* data, std::size_t size)
{
	DWORD old = 0;
	if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old))
		return false;
	std::memcpy(reinterpret_cast<void*>(address), data, size);
	VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
	return true;
}

bool Nop(std::uintptr_t address, std::size_t size)
{
	DWORD old = 0;
	if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old))
		return false;
	std::memset(reinterpret_cast<void*>(address), 0x90, size);
	VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
	return true;
}

std::uintptr_t HookCall(std::uintptr_t site, void* target)
{
	if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8)
		return 0;
	std::int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), sizeof(rel));
	const std::uintptr_t previous = site + 5 + rel;

	const std::int32_t newRel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target) - (site + 5));
	return Write(site + 1, &newRel, sizeof(newRel)) ? previous : 0;
}

void* HookImport(const char* dll, const char* function, void* replacement)
{
	// The loader already resolved the imports: find the slot by the address it holds. (Matching on
	// the import names would miss executables whose name table was stripped.)
	HMODULE library = GetModuleHandleA(dll);
	void* resolved = library ? reinterpret_cast<void*>(GetProcAddress(library, function)) : nullptr;
	if (!resolved)
		return nullptr;

	auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress)
		return nullptr;

	for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc)
	{
		if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), dll) != 0)
			continue;
		for (auto* slot = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk); slot->u1.Function; ++slot)
		{
			if (reinterpret_cast<void*>(slot->u1.Function) != resolved)
				continue;
			const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(replacement);
			return Write(reinterpret_cast<std::uintptr_t>(&slot->u1.Function), &value, sizeof(value)) ? resolved : nullptr;
		}
	}
	return nullptr;
}

void* HookVtable(void* object, std::size_t index, void* replacement)
{
	void** table = *reinterpret_cast<void***>(object);
	void* previous = table[index];
	// Already ours (objects of one class share the table): the caller keeps its saved original.
	if (previous == replacement)
		return nullptr;
	return Write(reinterpret_cast<std::uintptr_t>(&table[index]), &replacement, sizeof(replacement)) ? previous : nullptr;
}

}
