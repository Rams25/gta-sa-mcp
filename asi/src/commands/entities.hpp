// Entity enumeration shared by get_nearby_entities and capture_scene.
#pragma once

#include "common.hpp"

#include <vector>

namespace cmd
{

struct Found
{
	void* entity;
	float distance;
};

// Entities of the selected pools within `radius` of `centre`, nearest first. `model` < 0: any.
// `renderedOnly` keeps those the game currently has a 3D model for.
std::vector<Found> FindEntities(const game::Vec3& centre, float radius, const game::RayFilter& types, int model, bool renderedOnly);

}
