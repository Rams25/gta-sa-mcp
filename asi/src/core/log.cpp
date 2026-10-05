#include "log.hpp"

#include <windows.h>

#include <cstdio>
#include <ctime>
#include <mutex>

namespace
{

HMODULE PluginModule()
{
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&PluginModule), &self);
	return self;
}

}

std::wstring PluginDirectory()
{
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(PluginModule(), path, MAX_PATH);
	std::wstring dir(path);
	return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

void Log(const std::string& message)
{
	static std::mutex mutex;
	std::lock_guard<std::mutex> lock(mutex);

	FILE* file = nullptr;
	if (_wfopen_s(&file, (PluginDirectory() + L"gta-sa-mcp.log").c_str(), L"a") != 0 || !file)
		return;

	std::time_t now = std::time(nullptr);
	std::tm local {};
	localtime_s(&local, &now);
	char stamp[32];
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
	std::fprintf(file, "[%s] %s\n", stamp, message.c_str());
	std::fclose(file);
}

std::string ToUtf8(const std::wstring& text)
{
	if (text.empty())
		return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
	return out;
}

std::wstring FromUtf8(const std::string& text)
{
	if (text.empty())
		return {};
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
	return out;
}
