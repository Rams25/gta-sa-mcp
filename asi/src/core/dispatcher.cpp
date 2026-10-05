#include "dispatcher.hpp"

#include "config.hpp"
#include "log.hpp"
#include "../game/sdk.hpp"

#include <windows.h>
#include <float.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>

namespace
{

struct Command
{
	Phase phase;
	Handler handler;
	bool mutates;
};

struct Outcome
{
	json result;
	std::string errorCode;
	std::string errorMessage;
};

struct Pending
{
	const Command* command = nullptr;
	json params;
	std::promise<Outcome> done;
	std::atomic<bool> abandoned { false };
};

std::map<std::string, Command>& Commands()
{
	static std::map<std::string, Command> commands;
	return commands;
}

std::mutex g_queueMutex;
std::deque<std::shared_ptr<Pending>> g_queue;
std::atomic<unsigned> g_frame { 0 };
unsigned g_readyFrame = 0; // game thread only

void RunHandler(const Command& command, const json& params, Outcome& out)
{
	try
	{
		out.result = command.handler(params);
	}
	catch (const CommandError& e)
	{
		out.errorCode = e.code;
		out.errorMessage = e.what();
	}
	catch (const json::exception& e)
	{
		out.errorCode = "bad_params";
		out.errorMessage = e.what();
	}
	catch (const std::exception& e)
	{
		out.errorCode = "internal";
		out.errorMessage = e.what();
	}
}

// A bad pointer inside a game function must fail the request, not close the game. No C++ objects
// may live in this function (SEH and unwinding don't mix).
DWORD RunGuarded(const Command& command, const json& params, Outcome& out)
{
	__try
	{
		RunHandler(command, params, out);
		return 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return GetExceptionCode();
	}
}

void Run(const Command& command, const json& params, Outcome& out)
{
	// Direct3D leaves the game thread's FPU in single precision, which would round every double we
	// compute (12.344 would be sent as 12.343999862670898). Full precision while a command runs.
	unsigned int saved = 0, ignored = 0;
	_controlfp_s(&saved, 0, 0);
	_controlfp_s(&ignored, _PC_53, _MCW_PC);
	const DWORD exception = RunGuarded(command, params, out);
	_controlfp_s(&ignored, saved & _MCW_PC, _MCW_PC);
	if (exception == 0)
		return;
	char text[96];
	std::snprintf(text, sizeof(text), "the game raised exception 0x%08lX while running the command", exception);
	out.errorCode = "game_exception";
	out.errorMessage = text;
	Log(std::string("Command failed: ") + text);
}

void Pump(Phase phase)
{
	for (;;)
	{
		std::shared_ptr<Pending> request;
		{
			std::lock_guard<std::mutex> lock(g_queueMutex);
			if (g_queue.empty())
				return;
			// Requests run in arrival order: a screenshot queued first is not overtaken.
			if (g_queue.front()->command->phase != phase || g_frame.load() < g_readyFrame)
				return;
			request = g_queue.front();
			g_queue.pop_front();
		}
		if (request->abandoned.load())
			continue;

		Outcome outcome;
		Run(*request->command, request->params, outcome);
		if (request->command->mutates)
			g_readyFrame = g_frame.load() + static_cast<unsigned>(GetConfig().settleFrames);
		request->done.set_value(std::move(outcome));
	}
}

std::string Serialize(const json& response)
{
	// Model names come from game files in an unknown code page: never throw on them.
	return response.dump(-1, ' ', false, json::error_handler_t::replace);
}

json ErrorBody(const std::string& code, const std::string& message)
{
	return { { "code", code }, { "message", message } };
}

}

namespace dispatcher
{

void Register(const char* method, Phase phase, Handler handler, bool mutates)
{
	Commands()[method] = Command { phase, handler, mutates };
}

std::vector<std::string> Methods()
{
	std::vector<std::string> names;
	for (const auto& entry : Commands())
		names.push_back(entry.first);
	return names;
}

std::string Handle(const std::string& line)
{
	json response = json::object();
	json request = json::parse(line, nullptr, false);
	if (!request.is_object())
	{
		response["id"] = nullptr;
		response["error"] = ErrorBody("bad_request", "the request is not a JSON object");
		return Serialize(response);
	}
	response["id"] = request.value("id", json(nullptr));

	const std::string method = request.value("method", std::string());
	const auto found = Commands().find(method);
	if (found == Commands().end())
	{
		response["error"] = ErrorBody("unknown_method", "unknown method '" + method + "'");
		return Serialize(response);
	}
	const Command& command = found->second;
	json params = request.contains("params") && request["params"].is_object() ? request["params"] : json::object();

	Outcome outcome;
	if (command.phase == Phase::Direct)
	{
		RunHandler(command, params, outcome);
	}
	else if (command.phase == Phase::Tick && !game::InGame())
	{
		outcome.errorCode = "game_not_ready";
		outcome.errorMessage = "the game is not in play yet (state " + std::to_string(game::GameState())
			+ "): wait and retry, get_status tells when it is ready";
	}
	else if (!game::RenderReady())
	{
		outcome.errorCode = "game_not_ready";
		outcome.errorMessage = "the game is still starting (state " + std::to_string(game::GameState()) + ")";
	}
	else
	{
		auto pending = std::make_shared<Pending>();
		pending->command = &command;
		pending->params = std::move(params);
		std::future<Outcome> done = pending->done.get_future();
		{
			std::lock_guard<std::mutex> lock(g_queueMutex);
			g_queue.push_back(pending);
		}
		if (done.wait_for(std::chrono::milliseconds(GetConfig().requestTimeoutMs)) == std::future_status::ready)
			outcome = done.get();
		else
		{
			pending->abandoned = true;
			outcome.errorCode = "timeout";
			outcome.errorMessage = "the game did not run the command in time (loading, paused or minimised?)";
		}
	}

	if (outcome.errorCode.empty())
		response["result"] = std::move(outcome.result);
	else
		response["error"] = ErrorBody(outcome.errorCode, outcome.errorMessage);
	return Serialize(response);
}

void PumpTick()
{
	Pump(Phase::Tick);
}

void PumpFrame()
{
	++g_frame;
	Pump(Phase::Frame);
}

unsigned FrameCount()
{
	return g_frame.load();
}

}
