//
// Created by bobi on 15. 9. 26.
//
//  Intermediate code statements: trees that do something (move, jump) instead
//  of computing a value.

#pragma once
#include <memory>
#include <string>
#include <vector>

#include "ImcExpr.h"

namespace Basic {
    struct ImcStmt {
        virtual ~ImcStmt() = default;
        virtual std::string toString() const = 0;
    };

    using ImcStmtPtr = std::unique_ptr<ImcStmt>;

    // dst <- src. The destination is a MEM or a TEMP; for a MEM, the copy is as
    // wide as its size, which is how records are assigned.
    struct ImcMOVE : ImcStmt {
        ImcExprPtr dst;
        ImcExprPtr src;

        ImcMOVE(ImcExprPtr dst, ImcExprPtr src) : dst(std::move(dst)), src(std::move(src)) {}

        std::string toString() const override {
            return "MOVE(" + dst->toString() + ", " + src->toString() + ")";
        }
    };

    // Evaluate for the side effect and throw the value away (a call as a statement).
    struct ImcESTMT : ImcStmt {
        ImcExprPtr expr;

        explicit ImcESTMT(ImcExprPtr expr) : expr(std::move(expr)) {}

        std::string toString() const override { return "ESTMT(" + expr->toString() + ")"; }
    };

    struct ImcJUMP : ImcStmt {
        ImcLabel label;

        explicit ImcJUMP(ImcLabel label) : label(std::move(label)) {}

        std::string toString() const override { return "JUMP(" + label.name + ")"; }
    };

    // Jump to `pos` if the condition is non-zero, to `neg` otherwise.
    struct ImcCJUMP : ImcStmt {
        ImcExprPtr cond;
        ImcLabel pos;
        ImcLabel neg;

        ImcCJUMP(ImcExprPtr cond, ImcLabel pos, ImcLabel neg)
            : cond(std::move(cond)), pos(std::move(pos)), neg(std::move(neg)) {}

        std::string toString() const override {
            return "CJUMP(" + cond->toString() + ", " + pos.name + ", " + neg.name + ")";
        }
    };

    struct ImcLABEL : ImcStmt {
        ImcLabel label;

        explicit ImcLABEL(ImcLabel label) : label(std::move(label)) {}

        std::string toString() const override { return "LABEL(" + label.name + ")"; }
    };

    // A sequence, run in order.
    struct ImcSTMTS : ImcStmt {
        std::vector<ImcStmtPtr> stmts;

        ImcSTMTS() = default;
        explicit ImcSTMTS(std::vector<ImcStmtPtr> stmts) : stmts(std::move(stmts)) {}

        void add(ImcStmtPtr stmt) { stmts.push_back(std::move(stmt)); }

        std::string toString() const override {
            std::string out = "STMTS(";
            for (std::size_t i = 0; i < stmts.size(); ++i)
                out += (i ? ", " : "") + stmts[i]->toString();
            return out + ")";
        }
    };

    // Run a statement, then yield a value (e.g. the result temp of a && b).
    struct ImcSEXPR : ImcExpr {
        ImcStmtPtr stmt;
        ImcExprPtr expr;

        ImcSEXPR(ImcStmtPtr stmt, ImcExprPtr expr) : stmt(std::move(stmt)), expr(std::move(expr)) {}

        std::string toString() const override {
            return "SEXPR(" + stmt->toString() + ", " + expr->toString() + ")";
        }
    };
}
