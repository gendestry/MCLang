//
// Created by bobi on 15. 9. 26.
//
//  A constant. The language's only number type is float, so a constant holds a
//  double; addresses, offsets, sizes and booleans (0 or 1) are whole numbers.

#pragma once
#include "ImcGen/data/expr/ImcExpr.h"

namespace Basic {
    struct ImcCONST : ImcExpr {
        double value = 0;

        explicit ImcCONST(double value) : value(value) {}

        void accept(ImcExprVisitor &v) override { v.visit(*this); }

        // Whole numbers print as "8" rather than "8.000000".
        std::string toString() const override {
            std::string s = std::to_string(value);
            s.erase(s.find_last_not_of('0') + 1);
            if (s.back() == '.')
                s.pop_back();
            return "CONST(" + s + ")";
        }
    };
}
