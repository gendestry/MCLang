//
// Created by bobi on 16. 9. 26.
//
//  The passes that run on linearized code, one class each. Every pass rewrites
//  one LinCodeChunk in place and returns how many changes it made, so the driver
//  (LinOptimizer) can run them all again until none changes anything: each one
//  leaves work for the others (propagated copies leave dead moves, removed code
//  leaves jumps to thread, threaded jumps leave labels nobody uses).
//
//  Temps are per call in intermediate code -- a call can't change its caller's
//  temps (McGen saves the ones that could be clobbered) -- so only a MOVE ever
//  changes a temp. Blocks start at labels; facts that only hold within a block
//  are dropped at every label, since another block may jump there.

#pragma once
#include <cstddef>

#include "ImcLin/data/LinCodeChunk.h"

namespace Basic {
    // After MOVE(a, b), MOVE(a, 5) or MOVE(a, NAME), later reads of `a` in the same
    // block read `b` (the constant, the name) directly -- until `a` or `b` changes.
    class CopyPropagation {
    public:
        static std::size_t run(LinCodeChunk &chunk);
    };

    // Removes what can never matter:
    //   - statements after a jump and before the next label (unreachable)
    //   - a MOVE into a temp that isn't live afterwards (a call stays, as ESTMT)
    //   - an ESTMT with no call in it
    class DeadCode {
    public:
        static std::size_t run(LinCodeChunk &chunk);
    };

    // Tidies control flow. Every label is its own .mcfunction in the datapack and
    // every jump a function call, so each removed one saves a file and a call:
    //   - a jump to a label whose block is only another jump goes straight on
    //   - CJUMP(5, a, b) and CJUMP(x, a, a) become plain jumps
    //   - a jump to the label right after it is dropped
    //   - a label nobody jumps to is dropped: fallen into, its block merges with
    //     the one before; not fallen into, the block is unreachable and goes too
    class JumpThreading {
    public:
        static std::size_t run(LinCodeChunk &chunk);
    };

    // Common subexpression elimination within a block: MOVE(a, E) when an earlier
    // MOVE(b, E) is still valid becomes MOVE(a, b). A value is no longer valid once
    // a temp it reads (or `b`) changes, or -- when it reads memory -- once memory
    // may have been written (a store, a call or a raw command).
    class CommonSubexpr {
    public:
        static std::size_t run(LinCodeChunk &chunk);
    };

    // Loop-invariant code motion. A loop is what ImcGen makes of while and for: a
    // label, a stretch of code only entered through that label, and a jump back to
    // it. MOVE(t, E) moves in front of the loop when
    //   - nothing in the loop changes what E reads, and E can't fail (no division)
    //     or call anything, and reads no memory if the loop may write some
    //   - t is written nowhere else in the loop
    //   - t isn't live at the top of the loop, so no path sees the earlier write
    //     (including leaving without a single iteration)
    // and the loop is entered by falling into it, so there is a place in front.
    class LoopInvariant {
    public:
        static std::size_t run(LinCodeChunk &chunk);
    };

    // Runs everything above, plus constant folding on each statement, until
    // nothing changes.
    class LinOptimizer {
    public:
        static std::size_t run(LinCodeChunk &chunk);
    };
}
