// Settings read once from gta-sa-mcp.ini next to the .asi (every key is optional).
#pragma once

#include <string>

struct Config
{
	// [ipc]
	std::string pipe = "gta-sa-mcp"; // \\.\pipe\<pipe>
	int requestTimeoutMs = 20000;

	// [window]
	bool windowed = true;       // force a windowed Direct3D device
	int x = 40, y = 40;         // window position; negative values put it off screen
	bool noActivate = true;     // never take the focus or the mouse from the desktop
	bool runInBackground = true; // keep simulating and rendering without the focus

	// [input] Neutralize physical gameplay pad input even when no MCP lease is active.
	// Does not suppress SA-MP chat/UI hotkeys or OS keyboard polling.
	bool isolatePhysicalInput = false;

	// [startup]
	bool skipIntro = true;  // skip logo and intro movies
	bool autoStart = true;  // start a new game from the main menu without input
	bool scripts = false;   // run main.scm (missions, intro cutscene); off = empty sandbox
	float spawnX = 2495.0f, spawnY = -1687.0f, spawnZ = 13.6f;

	// [world]
	bool traffic = false;     // ambient peds and cars
	bool invincible = true;   // the player takes no damage
	bool hud = false;         // radar, health... (off keeps screenshots clean)

	// [capture]
	int settleFrames = 3;     // frames rendered after a change before the next command runs
	int jpegQuality = 80;
	int maxWidth = 1280;      // screenshots wider than this are downscaled
};

const Config& GetConfig();
