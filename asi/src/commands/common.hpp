// Shared by the command files: the registration entry points and JSON <-> game conversions.
#pragma once

#include "../core/dispatcher.hpp"
#include "../game/camera.hpp"
#include "../game/sdk.hpp"

#include <string>

// One per file in this folder; each registers its commands with the dispatcher.
void RegisterSessionCommands();
void RegisterPlayerCommands();
void RegisterCameraCommands();
void RegisterEntityCommands();
void RegisterWorldCommands();
void RegisterModelCommands();
void RegisterObjectCommands();
void RegisterCaptureCommands();
void RegisterAssetCommands();

namespace cmd
{

using game::Vec3;

// Numbers go out rounded: 13.547 instead of 13.546899795532227.
double Round(float value, int decimals = 3);
json ToJson(const Vec3& v, int decimals = 3);

// A position is [x, y, z] or {"x":, "y":, "z":}.
Vec3 ParseVec3(const json& value, const char* what);
Vec3 RequireVec3(const json& params, const char* key);
bool OptionalVec3(const json& params, const char* key, Vec3& out);

// World entities are named "<pool>:<handle>", e.g. "building:118272".
std::string RefOf(void* entity);
// Throws CommandError when the reference is malformed or the entity is gone.
void* ResolveRef(const std::string& ref);
// The entity named by params.entity (a reference) or params.object_id (an object we created).
void* RequireEntity(const json& params);

// A model is an id or a name from the .ide files. Throws when unknown.
int ResolveModel(const json& value);

// "types": ["building", "object", ...] as a filter; everything when absent.
game::RayFilter ParseFilter(const json& params, const char* key, const game::RayFilter& fallback);

// Short description: ref, type, model, position, rotation, plus object_id for our objects.
json Describe(void* entity);
// Model-space box and sphere plus the eight world-space corners; null without collision model.
json DescribeBounds(void* entity);
// Screen rectangle of the entity's bounding box, or null when it is entirely behind the camera.
json ScreenRect(const game::camera::View& view, void* entity);
json DescribeView(const game::camera::View& view);
json DescribeHit(bool hit, const Vec3& from, const game::ColPoint& point, void* entity);

}
