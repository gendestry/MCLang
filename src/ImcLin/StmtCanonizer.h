//
// Created by bobi on 15. 9. 26.
//
//  Flattens a statement tree into a list of plain statements: STMTS disappear,
//  and every expression inside is canonized (see ExprCanonizer). Destructive in
//  the same way ExprCanonizer is.

#pragma once
#include <vector>

#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    class StmtCanonizer : public ImcStmtVisitor {
    public:
        std::vector<ImcStmtPtr> canonize(ImcStmt &stmt);

        void visit(ImcMOVE &s) override;
        void visit(ImcESTMT &s) override;
        void visit(ImcCMD &s) override;
        void visit(ImcJUMP &s) override;
        void visit(ImcCJUMP &s) override;
        void visit(ImcLABEL &s) override;
        void visit(ImcSTMTS &s) override;

    private:
        std::vector<ImcStmtPtr> m_out; // the list the current canonize() is building
    };
}
