//
// Created by bobi on 15. 9. 26.
//
//  The value held in a temporary.

#pragma once
#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/names/ImcTemp.h"

namespace Basic {
    struct ImcTEMP : ImcExpr {
        ImcTemp temp;

        explicit ImcTEMP(ImcTemp temp) : temp(temp) {}

        void accept(ImcExprVisitor &v) override { v.visit(*this); }
        std::string toString() const override { return "TEMP(" + temp.toString() + ")"; }
    };
}
