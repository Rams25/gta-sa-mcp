#include "catalog.hpp"

#include "../core/log.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <unordered_map>

namespace game::catalog
{

namespace
{

struct Data
{
	std::vector<Entry> entries;
	std::unordered_map<int, std::size_t> byId;
	std::unordered_map<std::string, std::size_t> byName;
};

std::wstring GameDirectory()
{
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring dir(path);
	return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

std::string Trim(const std::string& text)
{
	const char* blank = " \t\r\n";
	const std::size_t first = text.find_first_not_of(blank);
	if (first == std::string::npos)
		return {};
	return text.substr(first, text.find_last_not_of(blank) - first + 1);
}

std::string Lower(std::string text)
{
	for (char& c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
}

std::vector<std::string> Split(const std::string& line)
{
	std::vector<std::string> fields;
	std::size_t start = 0;
	for (;;)
	{
		const std::size_t comma = line.find(',', start);
		fields.push_back(Trim(line.substr(start, comma == std::string::npos ? comma : comma - start)));
		if (comma == std::string::npos)
			return fields;
		start = comma + 1;
	}
}

bool IsModelSection(const std::string& section)
{
	return section == "objs" || section == "tobj" || section == "anim" || section == "weap"
		|| section == "cars" || section == "peds" || section == "hier";
}

void ReadIde(const std::wstring& gameDir, const std::string& relative, std::vector<Entry>& out)
{
	std::ifstream file(gameDir + FromUtf8(relative));
	if (!file)
		return;

	std::string section, line;
	while (std::getline(file, line))
	{
		line = Trim(line);
		if (line.empty() || line[0] == '#')
			continue;
		if (section.empty())
		{
			section = Lower(line);
			continue;
		}
		if (Lower(line) == "end")
		{
			section.clear();
			continue;
		}
		if (!IsModelSection(section))
			continue;

		const std::vector<std::string> fields = Split(line);
		if (fields.size() < 3 || fields[0].empty() || !std::isdigit(static_cast<unsigned char>(fields[0][0])))
			continue;

		Entry entry;
		entry.id = std::atoi(fields[0].c_str());
		entry.name = fields[1];
		entry.lower = Lower(fields[1]);
		entry.txd = fields[2];
		entry.section = section;
		entry.file = relative;
		// objs: id, model, txd, [mesh count,] draw distance, flags (tobj and anim add fields after).
		if (section == "objs" || section == "tobj" || section == "anim")
		{
			const std::size_t base = section == "objs" ? 5u : section == "tobj" ? 7u : 6u;
			const std::size_t at = section == "anim" ? 4u : fields.size() > base ? 4u : 3u;
			if (at < fields.size())
				entry.drawDistance = static_cast<float>(std::atof(fields[at].c_str()));
		}
		out.push_back(std::move(entry));
	}
}

Data Build()
{
	Data data;
	const std::wstring gameDir = GameDirectory();
	for (const wchar_t* list : { L"data\\default.dat", L"data\\gta.dat" })
	{
		std::ifstream file(gameDir + list);
		std::string line;
		while (std::getline(file, line))
		{
			line = Trim(line);
			if (line.size() > 4 && Lower(line.substr(0, 4)) == "ide ")
				ReadIde(gameDir, Trim(line.substr(4)), data.entries);
		}
	}

	std::stable_sort(data.entries.begin(), data.entries.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
	for (std::size_t i = 0; i < data.entries.size(); ++i)
	{
		data.byId.emplace(data.entries[i].id, i);
		data.byName.emplace(data.entries[i].lower, i);
	}
	Log("Model catalogue: " + std::to_string(data.entries.size()) + " models read from the .ide files.");
	return data;
}

const Data& Get()
{
	static const Data data = Build();
	return data;
}

}

const std::vector<Entry>& All()
{
	return Get().entries;
}

const Entry* ById(int id)
{
	const Data& data = Get();
	const auto found = data.byId.find(id);
	return found == data.byId.end() ? nullptr : &data.entries[found->second];
}

const Entry* ByName(const std::string& name)
{
	const Data& data = Get();
	const auto found = data.byName.find(Lower(name));
	return found == data.byName.end() ? nullptr : &data.entries[found->second];
}

std::string NameOf(int id)
{
	const Entry* entry = ById(id);
	return entry ? entry->name : std::string();
}

}
