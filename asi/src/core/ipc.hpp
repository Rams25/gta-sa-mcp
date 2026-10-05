// Local transport: a named pipe carrying one JSON request per line and one JSON response per line.
#pragma once

namespace ipc
{

// Starts the listener thread. Several clients may be connected at once.
void Start();

}
