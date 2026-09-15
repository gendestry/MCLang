//
// Created by bobi on 15. 9. 26.
//
//  Jump to `pos` if the condition is non-zero, to `neg` otherwise.

#pragma once
#include "ImcGen/data/stmt/ImcStmt.h"
#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/names/ImcLabel.h"

namespace Basic {
    struct ImcCJUMP : ImcStmt {
        ImcExprPtr cond;
        ImcLabel pos;
        ImcLabel neg;

        ImcCJUMP(ImcExprPtr cond, ImcLabel pos, ImcLabel neg)
            : cond(std::move(cond)), pos(std::move(pos)), neg(std::move(neg)) {}

        void accept(ImcStmtVisitor &v) override { v.visit(*this); }
        std::string toString() const override {
            return "CJUMP(" + cond->toString() + ", " + pos.name + ", " + neg.name + ")";
        }
    };
}
