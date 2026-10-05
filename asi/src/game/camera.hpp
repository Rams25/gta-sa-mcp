// Reading the rendered camera, projecting through it, and driving it.
#pragma once

#include "sdk.hpp"

namespace game::camera
{

// What the last frame was rendered with.
struct View
{
	bool valid = false;
	Vec3 position, front, up, right; // orthonormal, world space
	float tanX = 1.0f, tanY = 1.0f;  // half extents of the view at depth 1
	float nearClip = 0.0f, farClip = 0.0f;
	int width = 0, height = 0;       // pixels

	float HeadingDegrees() const { return Deg(std::atan2(-front.x, front.y)); }
	float PitchDegrees() const { return Deg(std::asin(front.z < -1.0f ? -1.0f : front.z > 1.0f ? 1.0f : front.z)); }
	float FovXDegrees() const { return Deg(2.0f * std::atan(tanX)); }
	float FovYDegrees() const { return Deg(2.0f * std::atan(tanY)); }
};

View Read();

// World to pixels. False when the point is behind the camera; x and y may be off screen.
bool Project(const View& view, const Vec3& world, float& x, float& y, float& depth);
// Pixels to a world ray.
void Ray(const View& view, float x, float y, Vec3& origin, Vec3& direction);

// Unit vector for a heading (0 = north/+Y, counter-clockwise) and a pitch (up positive), in degrees.
Vec3 Direction(float headingDegrees, float pitchDegrees);

// The camera we placed, if any. The game keeps following the player otherwise.
struct Shot
{
	bool active = false;
	Vec3 position, target;
	float fov = 70.0f;
};
const Shot& Scripted();

// Fixed camera at `position` looking at `target`; takes effect on the next simulation step.
void Set(const Vec3& position, const Vec3& target, float fov);
// Back to the game's own camera.
void Restore();

}
