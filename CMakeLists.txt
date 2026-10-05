cmake_minimum_required(VERSION 3.21)
project(gta-sa-mcp LANGUAGES CXX VERSION 0.1.0)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "gta_sa.exe is 32-bit: configure with -A Win32.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the plugin needs no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
# JSON for the wire protocol (header only).
FetchContent_Declare(json
	URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)
# stb_image_write (public domain): PNG/JPEG encoding of screenshots.
FetchContent_Declare(stb
	GIT_REPOSITORY https://github.com/nothings/stb.git
	GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_MakeAvailable(stb)

add_library(gta-sa-mcp SHARED
	asi/src/main.cpp
	asi/src/core/bootstrap.cpp
	asi/src/core/config.cpp
	asi/src/core/dispatcher.cpp
	asi/src/core/ipc.cpp
	asi/src/core/log.cpp
	asi/src/core/memory.cpp
	asi/src/game/assets.cpp
	asi/src/game/camera.cpp
	asi/src/game/catalog.cpp
	asi/src/game/sdk.cpp
	asi/src/render/capture.cpp
	asi/src/render/window.cpp
	asi/src/editor/scene.cpp
	asi/src/commands/assets.cpp
	asi/src/commands/camera.cpp
	asi/src/commands/capture.cpp
	asi/src/commands/common.cpp
	asi/src/commands/entities.cpp
	asi/src/commands/models.cpp
	asi/src/commands/objects.cpp
	asi/src/commands/player.cpp
	asi/src/commands/session.cpp
	asi/src/commands/world.cpp
)
target_include_directories(gta-sa-mcp PRIVATE "${stb_SOURCE_DIR}")
target_compile_definitions(gta-sa-mcp PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
	GTA_SA_MCP_VERSION="${PROJECT_VERSION}")
target_compile_options(gta-sa-mcp PRIVATE /W4 /utf-8)
target_link_libraries(gta-sa-mcp PRIVATE nlohmann_json::nlohmann_json)
target_link_options(gta-sa-mcp PRIVATE /MAP)

# ASI loaders load any DLL renamed to .asi from the game folder.
set_target_properties(gta-sa-mcp PROPERTIES PREFIX "" SUFFIX ".asi")
