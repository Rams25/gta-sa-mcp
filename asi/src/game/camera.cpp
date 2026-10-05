#include "camera.hpp"

#include "../core/memory.hpp"

namespace game::camera
{

namespace
{

constexpr std::uintptr_t kTheCamera = 0xB6F028;
// The `70.0f` that CCam::Process_Fixed writes to the fixed camera's FOV every frame.
constexpr std::uintptr_t kFixedFov = 0x51D5B1;

Shot g_shot;

void SetFixedFov(float fov)
{
	// Only where the constant is the one we expect (a different executable has other code there).
	static const bool known = At<float>(kFixedFov) == 70.0f;
	if (known)
		mem::Set<float>(kFixedFov, fov);
}

}

View Read()
{
	View view;
	void* camera = At<void*>(0xC1703C); // Scene.m_pRwCamera
	void* frame = camera ? Field<void*>(camera, 0x4) : nullptr;
	if (!frame)
		return view;

	// RwFrame::ltm. RenderWare names the axes right/up/at: `at` (our Matrix::up slot) is the view
	// direction and `up` (our Matrix::forward slot) points to the top of the screen.
	const Matrix& ltm = Field<Matrix>(frame, 0x50);
	view.position = ltm.pos;
	view.front = ltm.up.Normalized();
	view.right = view.front.Cross(ltm.forward).Normalized();
	view.up = view.right.Cross(view.front);

	view.tanX = Field<float>(camera, 0x68); // RwCamera::viewWindow
	view.tanY = Field<float>(camera, 0x6C);
	view.nearClip = Field<float>(camera, 0x80);
	view.farClip = Field<float>(camera, 0x84);
	view.width = ScreenWidth();
	view.height = ScreenHeight();
	view.valid = view.width > 0 && view.height > 0 && view.tanX > 0.0f && view.tanY > 0.0f;
	return view;
}

bool Project(const View& view, const Vec3& world, float& x, float& y, float& depth)
{
	const Vec3 d = world - view.position;
	depth = d.Dot(view.front);
	if (depth < 0.05f)
		return false;
	x = (0.5f + 0.5f * d.Dot(view.right) / (depth * view.tanX)) * static_cast<float>(view.width);
	y = (0.5f - 0.5f * d.Dot(view.up) / (depth * view.tanY)) * static_cast<float>(view.height);
	return true;
}

void Ray(const View& view, float x, float y, Vec3& origin, Vec3& direction)
{
	const float nx = 2.0f * x / static_cast<float>(view.width) - 1.0f;
	const float ny = 1.0f - 2.0f * y / static_cast<float>(view.height);
	origin = view.position;
	direction = (view.front + view.right * (nx * view.tanX) + view.up * (ny * view.tanY)).Normalized();
}

Vec3 Direction(float headingDegrees, float pitchDegrees)
{
	const float h = Rad(headingDegrees), p = Rad(pitchDegrees);
	return { -std::sin(h) * std::cos(p), std::cos(h) * std::cos(p), std::sin(p) };
}

const Shot& Scripted()
{
	return g_shot;
}

void Set(const Vec3& position, const Vec3& target, float fov)
{
	if (fov < 10.0f) fov = 10.0f;
	if (fov > 140.0f) fov = 140.0f;

	// The game builds the camera's side vector from the world's up axis: a perfectly vertical view
	// has none, so lean it a hair.
	Vec3 aim = target;
	const float dx = aim.x - position.x, dy = aim.y - position.y;
	if (dx * dx + dy * dy < 1e-4f)
		aim.y += 0.02f;

	void* camera = reinterpret_cast<void*>(kTheCamera);
	const Vec3 noUpOffset {};
	// CCamera::SetCamPositionForFixedMode, then CCamera::TakeControlNoEntity(target, jump cut, script).
	reinterpret_cast<void (__thiscall*)(void*, const Vec3*, const Vec3*)>(0x50BEC0)(camera, &position, &noUpOffset);
	reinterpret_cast<void (__thiscall*)(void*, const Vec3*, int, int)>(0x50C8B0)(camera, &aim, 2, 1);
	SetFixedFov(fov);

	g_shot.active = true;
	g_shot.position = position;
	g_shot.target = target;
	g_shot.fov = fov;
}

void Restore()
{
	reinterpret_cast<void (__thiscall*)(void*)>(0x50BAB0)(reinterpret_cast<void*>(kTheCamera)); // RestoreWithJumpCut
	SetFixedFov(70.0f);
	g_shot = Shot {};
}

}
