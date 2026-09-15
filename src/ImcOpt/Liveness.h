//
// Created by bobi on 16. 9. 26.
//
//  Liveness analysis over a linearized code chunk: for every statement, which
//  temps may still be read after it runs. The classic backward dataflow:
//
//    out[s] = union of in[t] for every successor t of s
//    in[s]  = uses[s] + (out[s] - defs[s])
//
//  iterated until nothing changes. Successors come from the flat list: a JUMP
//  goes to its label, a CJUMP to both of its labels, anything else to the next
//  statement. A jump to the exit label (or off the end) has no successor.
//
//  Only ordinary temps are tracked: FP and RV are left out, since the prologue
//  and epilogue take care of FP and a call's result is copied out of RV at once.

#pragma once
#include <cstddef>
#include <set>
#include <vector>

#include "ImcGen/data/stmt/ImcStmt.h"
#include "ImcLin/data/LinCodeChunk.h"

namespace Basic {
    struct ImcCALL;

    class Liveness {
    public:
        using Temps = std::set<std::size_t>; // temp ids

        // The temps live after each statement, indexed like chunk.stmts.
        static std::vector<Temps> liveOut(const LinCodeChunk &chunk);

        // The temps a statement reads, and the ones it writes.
        static Temps uses(const ImcStmt &stmt);
        static Temps defs(const ImcStmt &stmt);

        // The call a statement makes: MOVE(TEMP, CALL) or ESTMT(CALL). Linearized
        // code has no call anywhere else.
        static const ImcCALL *callIn(const ImcStmt &stmt);
    };
}
