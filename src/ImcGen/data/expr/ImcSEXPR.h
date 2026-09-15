//
// Created by bobi on 15. 9. 26.
//
//  Run a statement, then yield a value (e.g. the result temp of a && b).

#pragma once
#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    struct ImcSEXPR : ImcExpr {
        ImcStmtPtr stmt;
        ImcExprPtr expr;

        ImcSEXPR(ImcStmtPtr stmt, ImcExprPtr expr) : stmt(std::move(stmt)), expr(std::move(expr)) {}

        void accept(ImcExprVisitor &v) override { v.visit(*this); }
        std::string toString() const override {
            return "SEXPR(" + stmt->toString() + ", " + expr->toString() + ")";
        }
    };
}
