//
// Created by bobi on 16. 9. 26.
//
//  Inlining: replace a call to a small function with a copy of its body. A call
//  in the datapack costs the argument stores, the prologue and the epilogue
//  (FP save and restore, SP moves) -- around 15 commands before the body does
//  anything -- so a short function is cheaper copied in than called.
//
//  Runs on the trees after promotion, and only inlines a function that
//
//    - is a leaf: it calls nothing, so it can't be recursive
//    - has no memory of its own left: every parameter was promoted (its body
//      starts with the loads Promoter puts there) and nothing else mentions FP
//    - takes only one-slot arguments
//    - is at most MAX_NODES nodes big
//
//  CALL(f, args) becomes
//
//      SEXPR(STMTS(MOVE(result, 0), MOVE(a1, arg1), ..., <body>, LABEL end), TEMP(result))
//
//  where the copy of the body has fresh temps and labels, its parameters are
//  the argument temps, a `return x` moves into `result` and jumps to `end`. The
//  function itself is kept: entry points and other callers still use it.

#pragma once
#include <cstddef>
#include <vector>

#include "ImcGen/ImcGen.h"

namespace Basic {
    class Inliner {
    public:
        static constexpr std::size_t MAX_NODES = 80;

        // Inlines into every function body; returns how many calls were inlined.
        static std::size_t run(std::vector<ImcGen::Function> &functions);
    };
}
