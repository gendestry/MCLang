//
// Created by bobi on 15. 9. 26.
//
//  dst <- src. The destination is a MEM or a TEMP; for a MEM, the copy is as
//  wide as its size, which is how records are assigned.

#pragma once
#include "ImcGen/data/stmt/ImcStmt.h"
#include "ImcGen/data/expr/ImcExpr.h"

namespace Basic {
    struct ImcMOVE : ImcStmt {
        ImcExprPtr dst;
        ImcExprPtr src;

        ImcMOVE(ImcExprPtr dst, ImcExprPtr src) : dst(std::move(dst)), src(std::move(src)) {}

        void accept(ImcStmtVisitor &v) override { v.visit(*this); }
        std::string toString() const override {
            return "MOVE(" + dst->toString() + ", " + src->toString() + ")";
        }
    };
}
