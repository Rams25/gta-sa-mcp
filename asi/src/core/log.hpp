#pragma once

#include <string>

// Folder of the .asi itself (the game folder), with a trailing backslash.
std::wstring PluginDirectory();

// Appends a timestamped line to gta-sa-mcp.log next to the .asi. Safe from any thread.
void Log(const std::string& message);

std::string ToUtf8(const std::wstring& text);
std::wstring FromUtf8(const std::string& text);
