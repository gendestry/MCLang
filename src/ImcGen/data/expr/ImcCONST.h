//
// Created by bobi on 15. 9. 26.
//
//  A constant: numbers, and booleans as 0 or 1.

#pragma once
#include "ImcGen/data/expr/ImcExpr.h"

namespace Basic {
    struct ImcCONST : ImcExpr {
        long long value = 0;

        explicit ImcCONST(long long value) : value(value) {}

        void accept(ImcExprVisitor &v) override { v.visit(*this); }
        std::string toString() const override { return "CONST(" + std::to_string(value) + ")"; }
    };
}
