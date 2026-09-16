//
// Created by bobi on 16. 9. 26.
//
//  Register promotion: keep a local or a parameter in a temp instead of in
//  memory. In the datapack a temp is one scoreboard entry, while a memory slot
//  costs a macro call on every read and every write, so this is the biggest
//  single saving for code that works on plain variables.
//
//  Runs on ImcGen's trees, before constant folding, when every variable of the
//  function is still spelled exactly as ImcGen wrote it: MEM8(ADD(TEMP(FP),
//  CONST(offset))). A frame slot is promoted when every mention of that address
//  is a one-slot MEM around it, i.e. the variable is only ever read and written.
//  Anything else keeps it in memory:
//
//    - &x passes the address itself
//    - a record or an array is read as a wider MEM, or through ADD(address, ...)
//
//  Every use becomes TEMP(t). A promoted parameter is loaded from its slot once,
//  at the top of the body; a promoted local starts at 0, the way a memory slot
//  reads before anything is written (dead code elimination removes that when
//  the variable is always assigned first).
//
//  Only valid while a variable is only reachable from its own function, which
//  holds because the language has no nested functions (a nested one would read
//  its outer locals through the static link).

#pragma once
#include <cstddef>

#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    class Promoter {
    public:
        // Rewrites `body` in place and returns how many variables were promoted.
        static std::size_t run(ImcStmtPtr &body);
    };
}
