//
// Created by bobi on 15. 9. 26.
//
#pragma once
#include "ImcGen/data/expr/ImcExpr.h"

namespace Basic {
    struct ImcUNOP : ImcExpr {
        enum class Oper { NEG, NOT };

        Oper oper;
        ImcExprPtr expr;

        ImcUNOP(Oper oper, ImcExprPtr expr) : oper(oper), expr(std::move(expr)) {}

        void accept(ImcExprVisitor &v) override { v.visit(*this); }
        std::string toString() const override {
            return std::string(oper == Oper::NEG ? "NEG" : "NOT") + "(" + expr->toString() + ")";
        }
    };
}
