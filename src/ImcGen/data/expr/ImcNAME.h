//
// Created by bobi on 15. 9. 26.
//
//  The address a label stands for (a global, a string literal, a function).

#pragma once
#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/names/ImcLabel.h"

namespace Basic {
    struct ImcNAME : ImcExpr {
        ImcLabel label;

        explicit ImcNAME(ImcLabel label) : label(std::move(label)) {}

        void accept(ImcExprVisitor &v) override { v.visit(*this); }
        std::string toString() const override { return "NAME(" + label.name + ")"; }
    };
}
