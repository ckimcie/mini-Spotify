#pragma once
// BRIDGE_HOST (your PC's LAN IP) goes in include/secrets.h. Optional there too: BRIDGE_PORT, BRIDGE_TOKEN.
#ifndef BRIDGE_PORT
#define BRIDGE_PORT 8765
#endif
#ifndef BRIDGE_TOKEN
#define BRIDGE_TOKEN ""        // must match MINI_TOKEN on the PC if you set one
#endif
#define POLL_MS 1000
#define MAX_ART_BYTES 120000
