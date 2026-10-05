// Model names. The game only keeps a hash of each model's name, so the catalogue is read from the
// same .ide files the game loads (data\default.dat and data\gta.dat list them).
#pragma once

#include <string>
#include <vector>

namespace game::catalog
{

struct Entry
{
	int id = -1;
	std::string name;    // as written in the .ide
	std::string lower;   // for searching
	std::string txd;
	std::string section; // objs, tobj, anim, weap, cars, peds, hier
	std::string file;    // .ide it comes from, relative to the game folder
	float drawDistance = 0.0f;
};

// Sorted by id. Built on first use; safe from any thread.
const std::vector<Entry>& All();
const Entry* ById(int id);
const Entry* ByName(const std::string& name); // case-insensitive
// Empty when the id is not in any .ide (models added at run time by another mod).
std::string NameOf(int id);

}
