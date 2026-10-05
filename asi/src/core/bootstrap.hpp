// Everything that makes the game usable without a person in front of it: the hooks into its main
// loop, the unattended start, and the quiet sandbox world.
#pragma once

namespace bootstrap
{

// Called once when the plugin is loaded, before the game's own start-up code runs.
void Install();

}
