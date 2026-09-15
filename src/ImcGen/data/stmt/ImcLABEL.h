//
// Created by bobi on 15. 9. 26.
//
#pragma once
#include "ImcGen/data/stmt/ImcStmt.h"
#include "ImcGen/data/names/ImcLabel.h"

namespace Basic {
    struct ImcLABEL : ImcStmt {
        ImcLabel label;

        explicit ImcLABEL(ImcLabel label) : label(std::move(label)) {}

        void accept(ImcStmtVisitor &v) override { v.visit(*this); }
        std::string toString() const override { return "LABEL(" + label.name + ")"; }
    };
}
