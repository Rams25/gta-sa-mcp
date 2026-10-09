#pragma once
#include <array>
#include <cstdint>

namespace game::input {
// GTA CControllerState: 24 signed shorts, NewState at CPad[0]+0.
using State = std::array<std::int16_t, 24>;
struct Status {
    bool available;
    bool isolated;
    unsigned remaining;
    unsigned applied;
    const char* reason;
};
bool Install();
void AfterUpdatePads();
void Start(const State& state, unsigned frames, unsigned timeoutMs);
void Release();
Status GetStatus();
}
