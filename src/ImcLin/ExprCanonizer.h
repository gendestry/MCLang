//
// Created by bobi on 15. 9. 26.
//
//  Rewrites an expression so nothing inside it has a side effect: every call,
//  and every operand of a binary operator, is first moved into a temporary by a
//  statement appended to `out`. What comes back is a small tree over temps,
//  constants, names and MEMs that can be evaluated in any order.
//
//  Destructive: children are moved out of the input tree, so what is left of it
//  afterwards is only fit to be destroyed.

#pragma once
#include <vector>

#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    class ExprCanonizer : public ImcExprVisitor {
    public:
        explicit ExprCanonizer(std::vector<ImcStmtPtr> &out) : m_out(out) {}

        ImcExprPtr canonize(ImcExpr &expr);

        // Canonize, then park the value where later statements cannot change it.
        // A record-sized MEM does not fit in a temp, so its address is parked
        // instead and the MEM is rebuilt around that.
        ImcExprPtr toTemp(ImcExpr &expr);

        void visit(ImcCONST &e) override;
        void visit(ImcNAME &e) override;
        void visit(ImcTEMP &e) override;
        void visit(ImcMEM &e) override;
        void visit(ImcUNOP &e) override;
        void visit(ImcBINOP &e) override;
        void visit(ImcCALL &e) override;
        void visit(ImcSEXPR &e) override;

    private:
        std::vector<ImcStmtPtr> &m_out;
        ImcExprPtr m_result; // what the most recent visit produced
    };
}
