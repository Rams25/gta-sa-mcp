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
	return HookModuleImport(GetModuleHandleW(nullptr), dll, function, replacement);
}

void* HookModuleImport(void* module, const char* dll, const char* function, void* replacement)
{
	if (!module) return nullptr;
	auto* base = reinterpret_cast<std::uint8_t*>(module);
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress)
		return nullptr;

	for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc)
	{
		// Matched by name, not by the address in the slot: Windows compatibility shims may already
		// have replaced that address with their own.
		if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), dll) != 0 || !desc->OriginalFirstThunk)
			continue;
		auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
		auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
		for (; names->u1.AddressOfData; ++names, ++slots)
		{
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
				continue;
			const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
			if (std::strcmp(byName->Name, function) != 0)
				continue;
			void* previous = reinterpret_cast<void*>(slots->u1.Function);
			if (previous == replacement) return previous;
			const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(replacement);
			return Write(reinterpret_cast<std::uintptr_t>(&slots->u1.Function), &value, sizeof(value)) ? previous : nullptr;
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
