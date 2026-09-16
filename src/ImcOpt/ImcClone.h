//
// Created by bobi on 16. 9. 26.
//
//  Deep copies of intermediate code. Every child is owned by its parent, so a
//  tree that has to appear twice (a propagated value, an inlined body) has to be
//  copied.
//
//  With a renaming, the copy also gets its own temps and labels: every temp and
//  label it meets gets a fresh one the first time and the same one after that,
//  so an inlined body can't collide with its caller. Entries put in beforehand
//  win, which is how the inliner maps parameters onto argument temps and the
//  callee's exit label onto the end of the inlined code. FP is never renamed,
//  and RV only when `rv` is set. NAME and CALL labels name globals and functions,
//  so they are never renamed either.

#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/names/ImcLabel.h"
#include "ImcGen/data/names/ImcTemp.h"
#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    struct ImcRenaming {
        std::unordered_map<std::size_t, ImcTemp> temps;    // old id -> new temp
        std::unordered_map<std::string, ImcLabel> labels;  // old name -> new label
        std::optional<ImcTemp> rv;                         // where RV goes, if anywhere
    };

    ImcExprPtr clone(const ImcExpr &expr, ImcRenaming *renaming = nullptr);
    ImcStmtPtr clone(const ImcStmt &stmt, ImcRenaming *renaming = nullptr);
}
