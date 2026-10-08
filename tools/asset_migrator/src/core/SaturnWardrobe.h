// The Saturn wardrobe (stage 1 / 6, room 0x1C) as an RDT patch.
//
// The Saturn runs this wardrobe in code, not from its room script: GAME5
// replaces room-action handler 9 with 0x060490B8 (the outfit is picked from the
// player's yaw, the question comes from the Saturn's own global messages 62-64)
// and its post-action 4 (0x0605CA84) writes scenario flags 112/113 and starts
// event 1. The disc RDT's event 0 is the PS1 one-shot costume script and never
// runs there.
//
// patchSaturnWardrobeRdt rewrites the room so the same behaviour is plain SCD -
// the way the Director's Cut's arrange wardrobe (Stage8/ROOM81C0) does its two
// mannequins - and the port needs no wardrobe code, only the outfit loader that
// reads flags 112/113:
//
//   area 4 (examining it faces -z: the Saturn's yaw 1..0x7FF)
//       event 0: offers outfit 2 (112 + 113); worn -> back to the default
//   area 5 (faces +z: the other yaw half)
//       event 3: offers outfit 1 (112 only);  worn -> back to the default
//
// The questions are the Saturn's texts byte for byte, as room messages 3-5.
// tools/saturn_wardrobe_rdt.py is the reference implementation and `check`s
// the migrated files against it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace re1 {

// Patches a decoded Saturn ROOM11Cv / ROOM61Cv in place. Fails, leaving `rdt`
// untouched, if the room is not the shape the patch was written against.
bool patchSaturnWardrobeRdt(std::vector<uint8_t>* rdt, std::string* why);

}  // namespace re1
