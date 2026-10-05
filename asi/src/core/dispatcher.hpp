// Command registry and the bridge between the pipe threads and the game thread.
//
// Every command is a named function taking and returning JSON. Game memory is only ever touched
// from the game thread: a pipe thread queues the request and sleeps until the game thread has run
// it, either during the simulation step (Tick) or once the frame is fully drawn (Frame).
#pragma once

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

// Thrown by a command to answer {"error": {"code", "message"}}.
struct CommandError : std::runtime_error
{
	std::string code;
	CommandError(std::string errorCode, const std::string& message) : std::runtime_error(message), code(std::move(errorCode)) {}
};

enum class Phase
{
	Direct, // answered on the pipe thread: must not touch game memory that can change
	Tick,   // game thread, after the simulation step, before the frame is drawn
	Frame,  // game thread, when the frame is drawn but not shown yet (screenshots)
};

using Handler = json (*)(const json& params);

namespace dispatcher
{

// `mutates`: the command changes what the next frame looks like; the following commands wait for
// Config::settleFrames frames so they observe the result.
void Register(const char* method, Phase phase, Handler handler, bool mutates = false);
std::vector<std::string> Methods();

// Pipe thread: one request line in, one response line out (without the newline).
std::string Handle(const std::string& line);

// Game thread.
void PumpTick();
void PumpFrame(); // also counts the frame
unsigned FrameCount();

}
