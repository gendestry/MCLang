//
// Created by bobi on 15. 9. 26.
//
//  A sequence, run in order.

#pragma once
#include <vector>
#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    struct ImcSTMTS : ImcStmt {
        std::vector<ImcStmtPtr> stmts;

        ImcSTMTS() = default;
        explicit ImcSTMTS(std::vector<ImcStmtPtr> stmts) : stmts(std::move(stmts)) {}

        void add(ImcStmtPtr stmt) { stmts.push_back(std::move(stmt)); }

        void accept(ImcStmtVisitor &v) override { v.visit(*this); }
        std::string toString() const override {
            std::string out = "STMTS(";
            for (std::size_t i = 0; i < stmts.size(); ++i)
                out += (i ? ", " : "") + stmts[i]->toString();
            return out + ")";
        }
    };
}
