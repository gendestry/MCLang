//
// Created by bobi on 16. 9. 26.
//
//  Constant folding and algebraic simplification over an intermediate code
//  tree. Runs on ImcGen's trees, before linearization splits every operand into
//  its own temp -- on the tree, `pts[1]` is still one expression and folds to a
//  single ADD(FP, -48).
//
//  Children are folded first, so a rewrite always sees already-folded operands:
//
//    - an operator over constants becomes a constant: MUL(1, 32) -> 32
//      (except division or modulo by a constant 0, left for runtime to report)
//    - identities: x + 0, x - 0, x * 1, x / 1 -> x; x * 0 -> 0 (when x has no call)
//    - constant offsets merge: (x + 8) + 16 -> x + 24, and x - c becomes x + -c so
//      subtractions merge too
//    - a CJUMP on a constant becomes a JUMP
//
//  Numbers are doubles here, as in the interpreter. McGen's fixed point may round
//  a runtime division slightly differently from the folded result (1/3*3 folds to
//  1 but computes to 0.999), so folding can only make results more precise.

#pragma once
#include <cstddef>
#include <string>

#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    struct ImcCONST;

    class ConstantFolder : public ImcExprVisitor, public ImcStmtVisitor {
    public:
        // Rewrites `body` in place and returns how many rewrites were made.
        std::size_t run(const std::string &function, ImcStmtPtr &body);

        // Print mode: one line per function with its rewrite count.
        void setPrint(bool on) { m_print = on; }
        bool printing() const { return m_print; }

        // ---- expressions: a visit that replaces its node leaves the new one in m_expr ----
        void visit(ImcCONST &e) override;
        void visit(ImcNAME &e) override;
        void visit(ImcTEMP &e) override;
        void visit(ImcMEM &e) override;
        void visit(ImcUNOP &e) override;
        void visit(ImcBINOP &e) override;
        void visit(ImcCALL &e) override;
        void visit(ImcSEXPR &e) override;

        // ---- statements: likewise, in m_stmt ----
        void visit(ImcMOVE &s) override;
        void visit(ImcESTMT &s) override;
        void visit(ImcCMD &s) override;
        void visit(ImcJUMP &s) override;
        void visit(ImcCJUMP &s) override;
        void visit(ImcLABEL &s) override;
        void visit(ImcSTMTS &s) override;

    private:
        // Folds the node in `slot`, and puts its replacement there if it has one.
        void fold(ImcExprPtr &slot);
        void fold(ImcStmtPtr &slot);

        static const ImcCONST *asConst(const ImcExprPtr &expr);
        // Whether dropping `expr` would drop a call's side effects.
        static bool hasCall(const ImcExpr &expr);

        void replace(ImcExprPtr with);
        void replace(ImcStmtPtr with);

        ImcExprPtr m_expr;
        ImcStmtPtr m_stmt;
        std::size_t m_rewrites = 0;
        bool m_print = false;
    };
}
