//
// Created by bobi on 16. 9. 26.
//
//  Peephole optimization over one block of generated commands. McGen emits every
//  value into a scratch holder ($e0, $e1, ...) first, so a lot of commands only
//  shuffle values around. The rules, applied until none matches:
//
//    - a constant goes straight into add / remove:
//        set $e0 8000; $e1 += $e0          ->  add $e1 8000
//    - a comparison with a constant becomes a `matches` range:
//        set $e1 0; if score $T2 < $e1     ->  if score $T2 matches ..-1
//    - a scratch computed only to be copied is computed in place:
//        $e0 = $T2; $e0 += $T3; $RV = $e0  ->  $RV = $T2; $RV += $T3
//    - no-ops go: add X 0, remove X 0, X = X
//
//  Only scratch holders are ever removed. McGen defines one afresh in every
//  statement that uses it, so a scratch is dead once its next mention in the
//  block redefines it, or the block ends.
//
//  A rewrite never reaches across a `function` call (a called function clobbers
//  every scratch, and runtime helpers read $slot and $mem) or, when it moves a
//  write earlier, across a `return` (the other block could see the write).

#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace Basic {
    class McPeephole {
    public:
        // Rewrites `lines` in place and returns how many rewrites were made.
        static std::size_t run(std::vector<std::string> &lines);
    };
}
