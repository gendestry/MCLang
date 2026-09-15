//
// Created by bobi on 15. 9. 26.
//
//  Evaluate for the side effect and throw the value away (a call as a statement).

#pragma once
#include "ImcGen/data/stmt/ImcStmt.h"
#include "ImcGen/data/expr/ImcExpr.h"

namespace Basic {
    struct ImcESTMT : ImcStmt {
        ImcExprPtr expr;

        explicit ImcESTMT(ImcExprPtr expr) : expr(std::move(expr)) {}

        void accept(ImcStmtVisitor &v) override { v.visit(*this); }
        std::string toString() const override { return "ESTMT(" + expr->toString() + ")"; }
    };
}
