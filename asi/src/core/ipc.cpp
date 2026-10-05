#include "ipc.hpp"

#include "config.hpp"
#include "dispatcher.hpp"
#include "log.hpp"

#include <windows.h>

#include <string>

namespace
{

// A screenshot answer is a few hundred kB of base64.
constexpr DWORD kOutBuffer = 1 << 20;
constexpr DWORD kInBuffer = 1 << 16;
// A request line longer than this is not one of ours.
constexpr std::size_t kMaxLine = 4 << 20;

bool WriteAll(HANDLE pipe, const std::string& data)
{
	std::size_t sent = 0;
	while (sent < data.size())
	{
		DWORD written = 0;
		if (!WriteFile(pipe, data.data() + sent, static_cast<DWORD>(data.size() - sent), &written, nullptr) || written == 0)
			return false;
		sent += written;
	}
	return true;
}

DWORD WINAPI Serve(LPVOID param)
{
	HANDLE pipe = static_cast<HANDLE>(param);
	std::string buffer;
	char chunk[8192];
	for (;;)
	{
		DWORD read = 0;
		if (!ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) || read == 0)
			break;
		buffer.append(chunk, read);

		bool alive = true;
		std::size_t newline;
		while (alive && (newline = buffer.find('\n')) != std::string::npos)
		{
			std::string line = buffer.substr(0, newline);
			buffer.erase(0, newline + 1);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.empty())
				continue;
			alive = WriteAll(pipe, dispatcher::Handle(line) + "\n");
		}
		if (!alive || buffer.size() > kMaxLine)
			break;
	}
	FlushFileBuffers(pipe);
	DisconnectNamedPipe(pipe);
	CloseHandle(pipe);
	return 0;
}

DWORD WINAPI Listen(LPVOID)
{
	const std::wstring name = L"\\\\.\\pipe\\" + FromUtf8(GetConfig().pipe);
	Log("Listening on \\\\.\\pipe\\" + GetConfig().pipe);
	for (;;)
	{
		HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
			PIPE_UNLIMITED_INSTANCES, kOutBuffer, kInBuffer, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE)
		{
			Log("CreateNamedPipe failed, error " + std::to_string(GetLastError()));
			Sleep(2000);
			continue;
		}
		if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
		{
			CloseHandle(pipe);
			continue;
		}
		HANDLE thread = CreateThread(nullptr, 0, Serve, pipe, 0, nullptr);
		if (thread)
			CloseHandle(thread);
		else
			CloseHandle(pipe);
	}
}

}

namespace ipc
{

void Start()
{
	HANDLE thread = CreateThread(nullptr, 0, Listen, nullptr, 0, nullptr);
	if (thread)
		CloseHandle(thread);
}

}
