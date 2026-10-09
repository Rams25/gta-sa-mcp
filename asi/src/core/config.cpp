#include "config.hpp"

#include "log.hpp"

#include <windows.h>

#include <cstdlib>

namespace
{

std::wstring g_file;

std::string Text(const wchar_t* section, const wchar_t* key, const std::string& fallback)
{
	wchar_t buffer[256] = {};
	GetPrivateProfileStringW(section, key, L"", buffer, 256, g_file.c_str());
	return buffer[0] ? ToUtf8(buffer) : fallback;
}

int Int(const wchar_t* section, const wchar_t* key, int fallback)
{
	return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, g_file.c_str()));
}

bool Bool(const wchar_t* section, const wchar_t* key, bool fallback)
{
	return Int(section, key, fallback ? 1 : 0) != 0;
}

float Float(const wchar_t* section, const wchar_t* key, float fallback)
{
	const std::string text = Text(section, key, "");
	return text.empty() ? fallback : static_cast<float>(std::atof(text.c_str()));
}

Config Load()
{
	g_file = PluginDirectory() + L"gta-sa-mcp.ini";

	Config c;
	c.pipe = Text(L"ipc", L"pipe", c.pipe);
	c.requestTimeoutMs = Int(L"ipc", L"request_timeout_ms", c.requestTimeoutMs);

	c.windowed = Bool(L"window", L"windowed", c.windowed);
	c.x = Int(L"window", L"x", c.x);
	c.y = Int(L"window", L"y", c.y);
	c.noActivate = Bool(L"window", L"no_activate", c.noActivate);
	c.runInBackground = Bool(L"window", L"run_in_background", c.runInBackground);
	c.isolatePhysicalInput = Bool(L"input", L"isolate_physical", c.isolatePhysicalInput);

	c.skipIntro = Bool(L"startup", L"skip_intro", c.skipIntro);
	c.autoStart = Bool(L"startup", L"auto_start", c.autoStart);
	c.scripts = Bool(L"startup", L"scripts", c.scripts);
	c.spawnX = Float(L"startup", L"spawn_x", c.spawnX);
	c.spawnY = Float(L"startup", L"spawn_y", c.spawnY);
	c.spawnZ = Float(L"startup", L"spawn_z", c.spawnZ);

	c.traffic = Bool(L"world", L"traffic", c.traffic);
	c.invincible = Bool(L"world", L"invincible", c.invincible);
	c.hud = Bool(L"world", L"hud", c.hud);

	c.settleFrames = Int(L"capture", L"settle_frames", c.settleFrames);
	c.jpegQuality = Int(L"capture", L"jpeg_quality", c.jpegQuality);
	c.maxWidth = Int(L"capture", L"max_width", c.maxWidth);
	return c;
}

}

const Config& GetConfig()
{
	static const Config config = Load();
	return config;
}
