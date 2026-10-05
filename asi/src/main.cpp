// gta-sa-mcp.asi: lets an MCP server observe, drive and edit GTA: San Andreas.
//
// Loaded into gta_sa.exe by any ASI loader. Layout:
//   core/      memory patching, logging, settings, the pipe transport, the command dispatcher,
//              and the hooks into the game's main loop
//   game/      what we know of the game's memory: entities, world queries, camera, model names
//   render/    windowed mode and screenshots
//   editor/    the objects placed in this session, their history and their files
//   commands/  one file per domain; each registers JSON commands with the dispatcher
// Adding a tool = writing a function in commands/ and registering it; the MCP server exposes it.
#include "commands/common.hpp"
#include "core/bootstrap.hpp"
#include "core/ipc.hpp"
#include "core/log.hpp"
#include "render/window.hpp"

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	DisableThreadLibraryCalls(module);

	Log("gta-sa-mcp " GTA_SA_MCP_VERSION " loaded.");
	RegisterSessionCommands();
	RegisterPlayerCommands();
	RegisterCameraCommands();
	RegisterEntityCommands();
	RegisterWorldCommands();
	RegisterModelCommands();
	RegisterObjectCommands();
	RegisterCaptureCommands();
	RegisterAssetCommands();

	bootstrap::Install();
	window::Install();
	ipc::Start();
	return TRUE;
}
