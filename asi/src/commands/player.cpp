#include "common.hpp"

namespace
{

using game::Vec3;

json PlayerState()
{
	void* ped = game::PlayerPed();
	void* vehicle = game::PlayerVehicle();
	void* body = vehicle ? vehicle : ped;

	json out = cmd::Describe(ped);
	out["position"] = cmd::ToJson(game::entity::Position(body));
	out["heading"] = cmd::Round(game::Deg(game::entity::Heading(body)), 2);
	out["velocity"] = cmd::ToJson(game::Field<Vec3>(body, 0x44) * 50.0f, 2); // per frame -> m/s
	out["health"] = cmd::Round(game::Field<float>(ped, 0x540), 1);
    // Read-only native evidence: GTA US CPad getters (53FB80 etc.) gate on this word.
    const auto controls = game::At<unsigned short>(0xB73458 + 0x10E);
    const auto flags = game::Field<unsigned>(ped, 0x1C);
    out["native_state"] = {
        {"ped_health",game::Field<float>(ped,0x540)},
        {"pad0_disable_player_controls",controls},
        {"pad0_controls_enabled",controls==0},
        {"ped_processing_flags",flags},
        {"ped_collision_enabled",(flags&1u)!=0},
        {"ped_gravity_processing_enabled",(flags&0x80000002u)==0},
        {"ped_gravity_flag_mask",flags&0x80000002u},
        {"sample_phase","game_tick"}
    };
	out["interior"] = game::entity::Area(ped);
	out["in_vehicle"] = vehicle != nullptr;
	out["vehicle"] = cmd::Describe(vehicle);

	float groundZ = 0.0f;
	const Vec3 position = game::entity::Position(body);
	if (game::world::GroundZ(position, groundZ))
		out["ground_z"] = cmd::Round(groundZ);
	return out;
}

json GetPlayer(const json&)
{
	return PlayerState();
}

json Teleport(const json& params)
{
	Vec3 position = cmd::RequireVec3(params, "position");
	void* ped = game::PlayerPed();
	void* vehicle = game::PlayerVehicle();
	void* body = vehicle ? vehicle : ped;

	// The map around the destination first, so there is ground to stand on.
	if (params.value("load_scene", true))
		game::streaming::LoadScene(position);

	bool snapped = false;
	if (params.value("snap_to_ground", false))
	{
		float groundZ = 0.0f;
		if (game::world::GroundZ({ position.x, position.y, position.z + 2.0f }, groundZ))
		{
			position.z = groundZ + 1.0f;
			snapped = true;
		}
	}

	// CEntity::Teleport(position, resetRotation)
	reinterpret_cast<void (__thiscall*)(void*, Vec3, bool)>(game::Virtual(body, 14))(body, position, false);
	game::Field<Vec3>(body, 0x44) = Vec3 {}; // move speed
	game::Field<Vec3>(body, 0x50) = Vec3 {}; // turn speed

	if (params.contains("heading") && params["heading"].is_number())
	{
		const float heading = game::Rad(params["heading"].get<float>());
		if (game::Matrix* matrix = game::entity::GetMatrix(body))
			game::SetRotation(*matrix, { 0.0f, 0.0f, game::Deg(heading) });
		if (!vehicle)
		{
			// A ped's matrix follows these two every frame.
			game::Field<float>(ped, 0x558) = heading; // m_fCurrentRotation
			game::Field<float>(ped, 0x55C) = heading; // m_fAimingRotation
		}
	}

	json out = PlayerState();
	out["snapped_to_ground"] = snapped;
	return out;
}

}

void RegisterPlayerCommands()
{
	dispatcher::Register("get_player", Phase::Tick, GetPlayer);
	dispatcher::Register("teleport", Phase::Tick, Teleport, true);
}
