#include "bootstrap.hpp"

#include "config.hpp"
#include "dispatcher.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "../game/assets.hpp"
#include "../game/sdk.hpp"

#include <windows.h>

namespace bootstrap
{

namespace
{

using game::At;
using game::Field;
using game::Vec3;

constexpr std::uintptr_t kMenuManager = 0xBA6748; // FrontEndMenuManager
constexpr std::uintptr_t kTheCamera = 0xB6F028;

std::uintptr_t g_gameProcess = 0;   // CGame::Process
std::uintptr_t g_showRaster = 0;    // RsCameraShowRaster, in Idle
std::uintptr_t g_showRasterMenu = 0; // the same, in FrontendIdle
std::uintptr_t g_frontendEvent = 0; // RsEventHandler(rsFRONTENDIDLE)
std::uintptr_t g_updatePads = 0;    // CPad::UpdatePads, in the intro movie states

int g_menuFrames = 0;
bool g_startRequested = false;
int g_fadeTicks = 0;

// ---- Unattended start ------------------------------------------------------------------------

// The logo and intro movies are not played at all (starting one and cutting it short leaves the
// sound system unable to initialise). Their two "playing" states normally end with the movie or a
// key press: end them right away.
void __cdecl SkipMovie(int, const char*)
{
}

void __cdecl OnIntroUpdatePads()
{
	reinterpret_cast<void (__cdecl*)()>(g_updatePads)();
	int& state = At<int>(0xC8D4C0);
	if (state == 2)
		state = 3; // logo -> title
	else if (state == 4)
		state = 5; // intro -> load the menu
}

// Main menu, once per frame: choose "New game" as the menu itself does.
int __cdecl OnFrontendIdle(int event, void* param)
{
	const int result = reinterpret_cast<int (__cdecl*)(int, void*)>(g_frontendEvent)(event, param);

	// Give the menu a few frames to finish setting itself up.
	if (!g_startRequested && At<bool>(kMenuManager + 0x5C) && ++g_menuFrames >= 5)
	{
		g_startRequested = true;
		At<std::uint8_t>(0xB72910) = 0; // CGame::bMissionPackGame
		reinterpret_cast<void (__thiscall*)(void*)>(0x573330)(reinterpret_cast<void*>(kMenuManager)); // DoSettingsBeforeStartingAGame
		At<bool>(kMenuManager + 0x32) = true; // m_bDontDrawFrontEnd: closes the menu, which starts the load
		Log("Main menu reached: starting a new game.");
	}
	return result;
}

// ---- Sandbox world ---------------------------------------------------------------------------

// What the mission script's CREATE_PLAYER does. Without main.scm nothing else creates the player.
void CreatePlayer()
{
	const Config& config = GetConfig();
	const Vec3 spawn { config.spawnX, config.spawnY, config.spawnZ };

	if (!game::streaming::IsLoaded(0))
	{
		// CStreaming::RequestSpecialModel(0, "player", game required | keep in memory | priority)
		reinterpret_cast<void (__cdecl*)(int, const char*, int)>(0x409D10)(0, "player", 0x2 | 0x8 | 0x10);
		reinterpret_cast<void (__cdecl*)(bool)>(0x40EA10)(true);
	}

	reinterpret_cast<void (__cdecl*)(int)>(0x60D790)(0); // CPlayerPed::SetupPlayerPed
	void* ped = reinterpret_cast<void* (__cdecl*)(int)>(0x56E210)(0);
	if (!ped)
	{
		Log("Could not create the player.");
		return;
	}
	Field<std::uint8_t>(ped, 0x484) = 2; // created by: mission (never removed by the population code)

	reinterpret_cast<void (__cdecl*)(int)>(0x609520)(0); // CPlayerPed::DeactivatePlayerPed
	if (game::Matrix* matrix = game::entity::GetMatrix(ped))
		matrix->pos = spawn;
	else
		Field<Vec3>(ped, 0x4) = spawn;
	reinterpret_cast<void (__cdecl*)(int)>(0x609540)(0); // CPlayerPed::ReactivatePlayerPed

	// Default task: stand and react to the controls.
	void* task = reinterpret_cast<void* (__cdecl*)(unsigned)>(0x61A5A0)(0x1C); // CTask::operator new
	reinterpret_cast<void* (__thiscall*)(void*)>(0x685750)(task);              // CTaskSimplePlayerOnFoot
	void* taskManager = Field<std::uint8_t*>(ped, 0x47C) + 4;                  // CPedIntelligence::m_TaskMgr
	reinterpret_cast<void (__thiscall*)(void*, void*, int, bool)>(0x681AF0)(taskManager, task, 4, false);

	game::streaming::LoadScene(spawn);

	At<std::uint8_t>(0xB70153) = 12; // noon
	At<std::uint8_t>(0xB70152) = 0;
	g_fadeTicks = 120;
	Log("Player created: sandbox world ready.");
}

// Replaces CTheScripts::Process when main.scm is off.
void __cdecl OnScripts()
{
	if (!reinterpret_cast<void* (__cdecl*)(int)>(0x56E210)(0))
		CreatePlayer();
}

void KeepWorldQuiet()
{
	const Config& config = GetConfig();
	if (!config.traffic)
	{
		At<float>(0x8D2530) = 0.0f; // CPopulation::PedDensityMultiplier
		At<float>(0x8A5B20) = 0.0f; // CCarCtrl::CarDensityMultiplier
	}
	void* ped = game::PlayerPed();
	if (ped && config.invincible)
		Field<std::uint8_t>(ped, 0x42) |= 0xFC; // bullet, fire, collision, melee, all, explosion proof

	// The new-game sequence leaves the screen faded to black (the mission script normally fades it
	// in) and reloads the player's display settings: undo both during the first seconds.
	if (g_fadeTicks > 0)
	{
		--g_fadeTicks;
		game::ShowHud(config.hud);
		// "Respect lost...": the stats reset of a new game announces itself. CHud::SetHelpMessage(none).
		reinterpret_cast<void (__cdecl*)(const char*, bool, bool, bool)>(0x588BE0)(nullptr, true, false, false);
		reinterpret_cast<void (__thiscall*)(void*, float, short)>(0x50AC20)(reinterpret_cast<void*>(kTheCamera), 0.0f, 1); // CCamera::Fade(in)
	}
}

// ---- Main loop -------------------------------------------------------------------------------

void __cdecl OnGameProcess()
{
	reinterpret_cast<void (__cdecl*)()>(g_gameProcess)();
	if (!game::PlayerPed())
		return;
	KeepWorldQuiet();
	game::assets::Tick();
	dispatcher::PumpTick();
}

void* __cdecl OnShowRaster(void* camera)
{
	dispatcher::PumpFrame();
	return reinterpret_cast<void* (__cdecl*)(void*)>(g_showRaster)(camera);
}

void* __cdecl OnShowRasterMenu(void* camera)
{
	dispatcher::PumpFrame();
	return reinterpret_cast<void* (__cdecl*)(void*)>(g_showRasterMenu)(camera);
}

template <typename Function>
bool Hook(std::uintptr_t site, Function* target, std::uintptr_t& original, const char* what)
{
	original = mem::HookCall(site, reinterpret_cast<void*>(target));
	if (!original)
		Log(std::string("Hook failed (") + what + "): this is not gta_sa.exe 1.0 US.");
	return original != 0;
}

}

void Install()
{
	const Config& config = GetConfig();

	// Without these two nothing works: stop here on an unknown executable rather than half-patch it.
	if (!Hook(0x53E981, OnGameProcess, g_gameProcess, "game process")
		|| !Hook(0x53EC01, OnShowRaster, g_showRaster, "frame end"))
		return;
	Hook(0x53E888, OnShowRasterMenu, g_showRasterMenu, "menu frame end");

	if (config.runInBackground)
	{
		mem::Nop(0x748A8D, 6);                    // main loop: don't sleep while another window has the focus
		mem::Set<std::uint8_t>(0x53BC78, 0);      // don't open the pause menu when the focus is lost
	}
	if (config.noActivate)
		mem::Set<std::uint8_t>(0x6194A0, 0xC3);   // RsMouseSetPos: don't drag the desktop's mouse to the window centre

	// Under SA-MP the multiplayer client owns the start-up sequence, the scripts and the player:
	// only observe and edit there.
	if (GetModuleHandleW(L"samp.dll"))
	{
		Log("SA-MP detected: unattended start and sandbox world are disabled.");
		return;
	}

	if (config.skipIntro)
	{
		std::uintptr_t play = 0;
		Hook(0x748B00, SkipMovie, play, "logo movie");
		Hook(0x748BF9, SkipMovie, play, "intro movie");
		Hook(0x748B17, OnIntroUpdatePads, g_updatePads, "intro skip");
		// The two sponsor splash screens: keep them, without their slow fade in and out.
		for (const std::uintptr_t fade : { 0x748AB6u, 0x748ABBu, 0x748AD4u, 0x748AD9u })
			mem::Nop(fade, 5);
	}
	if (config.autoStart)
		Hook(0x748CC2, OnFrontendIdle, g_frontendEvent, "auto start");

	if (!config.scripts)
	{
		// Every place the game runs the mission script: twice while a game starts (which is when
		// main.scm creates the player and launches the intro), then once per frame.
		std::uintptr_t unused = 0;
		Hook(0x53BCC9, OnScripts, unused, "scripts (init)");
		Hook(0x53BE8D, OnScripts, unused, "scripts (restart)");
		Hook(0x53BFC7, OnScripts, unused, "scripts");
	}
	Log("Game hooks installed.");
}

}
