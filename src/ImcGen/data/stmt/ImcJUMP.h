//
// Created by bobi on 15. 9. 26.
//
#pragma once
#include "ImcGen/data/stmt/ImcStmt.h"
#include "ImcGen/data/names/ImcLabel.h"

namespace Basic {
    struct ImcJUMP : ImcStmt {
        ImcLabel label;

        explicit ImcJUMP(ImcLabel label) : label(std::move(label)) {}

        void accept(ImcStmtVisitor &v) override { v.visit(*this); }
        std::string toString() const override { return "JUMP(" + label.name + ")"; }
    };
}
