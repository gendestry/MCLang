//
// Created by bobi on 16. 9. 26.
//

#include "ImcOpt/ConstantFolder.h"

#include <cmath>
#include <iostream>
#include <optional>

#include "ImcGen/data/expr/ImcBINOP.h"
#include "ImcGen/data/expr/ImcCALL.h"
#include "ImcGen/data/expr/ImcCONST.h"
#include "ImcGen/data/expr/ImcMEM.h"
#include "ImcGen/data/expr/ImcNAME.h"
#include "ImcGen/data/expr/ImcSEXPR.h"
#include "ImcGen/data/expr/ImcTEMP.h"
#include "ImcGen/data/expr/ImcUNOP.h"
#include "ImcGen/data/stmt/ImcCJUMP.h"
#include "ImcGen/data/stmt/ImcESTMT.h"
#include "ImcGen/data/stmt/ImcJUMP.h"
#include "ImcGen/data/stmt/ImcLABEL.h"
#include "ImcGen/data/stmt/ImcMOVE.h"
#include "ImcGen/data/stmt/ImcSTMTS.h"
#include "Utils/Colors/Font.h"

namespace Basic {
    namespace {
        using Oper = ImcBINOP::Oper;

        // The value of `a oper b`, as the interpreter would compute it. Nothing
        // for division or modulo by zero, which must still fail at runtime.
        std::optional<double> evaluate(Oper oper, double a, double b) {
            switch (oper) {
                case Oper::OR: return (a != 0 || b != 0) ? 1 : 0;
                case Oper::AND: return (a != 0 && b != 0) ? 1 : 0;
                case Oper::EQU: return a == b ? 1 : 0;
                case Oper::NEQ: return a != b ? 1 : 0;
                case Oper::LTH: return a < b ? 1 : 0;
                case Oper::GTH: return a > b ? 1 : 0;
                case Oper::LEQ: return a <= b ? 1 : 0;
                case Oper::GEQ: return a >= b ? 1 : 0;
                case Oper::ADD: return a + b;
                case Oper::SUB: return a - b;
                case Oper::MUL: return a * b;
                case Oper::DIV: if (b == 0) return std::nullopt; return a / b;
                case Oper::MOD: if (b == 0) return std::nullopt; return std::fmod(a, b);
            }
            return std::nullopt;
        }

        ImcExprPtr constant(double value) { return std::make_unique<ImcCONST>(value); }

        // x + c, written as x - |c| when c is negative so McGen never has to add a
        // negative number.
        ImcExprPtr offset(ImcExprPtr x, double c) {
            if (c < 0)
                return std::make_unique<ImcBINOP>(Oper::SUB, std::move(x), constant(-c));
            return std::make_unique<ImcBINOP>(Oper::ADD, std::move(x), constant(c));
        }
    }

    std::size_t ConstantFolder::run(const std::string &function, ImcStmtPtr &body) {
        m_rewrites = 0;
        if (body) {
            // One bottom-up walk folds almost everything; repeat until it settles
            // in case a rewrite exposed another.
            std::size_t before;
            do {
                before = m_rewrites;
                fold(body);
            } while (m_rewrites != before);
        }
        if (m_print)
            std::cout << "  " << Utils::Font::colorYellow << "'" << function << "'" << Utils::Font::colorReset
                      << Utils::Font::colorDim << " folded " << m_rewrites << " node(s)" << Utils::Font::colorReset
                      << std::endl;
        return m_rewrites;
    }

    void ConstantFolder::fold(ImcExprPtr &slot) {
        m_expr = nullptr;
        slot->accept(*this);
        if (m_expr)
            slot = std::move(m_expr);
    }

    void ConstantFolder::fold(ImcStmtPtr &slot) {
        m_stmt = nullptr;
        slot->accept(*this);
        if (m_stmt)
            slot = std::move(m_stmt);
    }

    const ImcCONST *ConstantFolder::asConst(const ImcExprPtr &expr) {
        return dynamic_cast<const ImcCONST *>(expr.get());
    }

    bool ConstantFolder::hasCall(const ImcExpr &expr) {
        if (dynamic_cast<const ImcCALL *>(&expr) || dynamic_cast<const ImcSEXPR *>(&expr))
            return true;
        if (const auto *mem = dynamic_cast<const ImcMEM *>(&expr))
            return hasCall(*mem->addr);
        if (const auto *unop = dynamic_cast<const ImcUNOP *>(&expr))
            return hasCall(*unop->expr);
        if (const auto *binop = dynamic_cast<const ImcBINOP *>(&expr))
            return hasCall(*binop->fst) || hasCall(*binop->snd);
        return false;
    }

    void ConstantFolder::replace(ImcExprPtr with) {
        m_expr = std::move(with);
        ++m_rewrites;
    }

    void ConstantFolder::replace(ImcStmtPtr with) {
        m_stmt = std::move(with);
        ++m_rewrites;
    }

    // ---- expressions ----

    void ConstantFolder::visit(ImcCONST &) {}
    void ConstantFolder::visit(ImcNAME &) {}
    void ConstantFolder::visit(ImcTEMP &) {}

    void ConstantFolder::visit(ImcMEM &e) { fold(e.addr); }

    void ConstantFolder::visit(ImcUNOP &e) {
        fold(e.expr);
        if (const ImcCONST *c = asConst(e.expr))
            replace(constant(e.oper == ImcUNOP::Oper::NEG ? -c->value : (c->value == 0 ? 1 : 0)));
    }

    void ConstantFolder::visit(ImcBINOP &e) {
        fold(e.fst);
        fold(e.snd);

        const ImcCONST *a = asConst(e.fst);
        const ImcCONST *b = asConst(e.snd);
        if (a && b) {
            if (const std::optional<double> value = evaluate(e.oper, a->value, b->value))
                replace(constant(*value));
            return;
        }

        // Put the constant of a commutative operator on the right, so the rules
        // below only have to look there.
        if (a && (e.oper == Oper::ADD || e.oper == Oper::MUL)) {
            std::swap(e.fst, e.snd);
            std::swap(a, b);
        }
        if (!b)
            return;

        const double c = b->value;
        switch (e.oper) {
            case Oper::ADD:
            case Oper::SUB: {
                const double delta = e.oper == Oper::ADD ? c : -c;
                if (delta == 0) {
                    replace(std::move(e.fst));
                    return;
                }
                // (x +- c1) +- c2 -> x +- (c1 +- c2)
                auto *inner = dynamic_cast<ImcBINOP *>(e.fst.get());
                if (inner && (inner->oper == Oper::ADD || inner->oper == Oper::SUB)) {
                    if (const ImcCONST *ic = asConst(inner->snd)) {
                        const double total = (inner->oper == Oper::ADD ? ic->value : -ic->value) + delta;
                        replace(total == 0 ? std::move(inner->fst) : offset(std::move(inner->fst), total));
                    }
                }
                return;
            }
            case Oper::MUL:
                if (c == 1)
                    replace(std::move(e.fst));
                else if (c == 0 && !hasCall(*e.fst))
                    replace(constant(0));
                return;
            case Oper::DIV:
                if (c == 1)
                    replace(std::move(e.fst));
                return;
            default:
                return;
        }
    }

    void ConstantFolder::visit(ImcCALL &e) {
        for (ImcExprPtr &arg : e.args)
            fold(arg);
    }

    void ConstantFolder::visit(ImcSEXPR &e) {
        fold(e.stmt);
        fold(e.expr);
    }

    // ---- statements ----

    void ConstantFolder::visit(ImcMOVE &s) {
        fold(s.dst);
        fold(s.src);
    }

    void ConstantFolder::visit(ImcESTMT &s) { fold(s.expr); }
    void ConstantFolder::visit(ImcJUMP &) {}
    void ConstantFolder::visit(ImcLABEL &) {}

    void ConstantFolder::visit(ImcCJUMP &s) {
        fold(s.cond);
        if (const ImcCONST *c = asConst(s.cond))
            replace(std::make_unique<ImcJUMP>(c->value != 0 ? s.pos : s.neg));
    }

    void ConstantFolder::visit(ImcSTMTS &s) {
        for (ImcStmtPtr &stmt : s.stmts)
            fold(stmt);
    }
}
