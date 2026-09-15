//
// Created by bobi on 15. 9. 26.
//
//  AND and OR here evaluate both sides; short-circuiting is done with jumps.

#pragma once
#include "ImcGen/data/expr/ImcExpr.h"

namespace Basic {
    struct ImcBINOP : ImcExpr {
        enum class Oper { OR, AND, EQU, NEQ, LTH, GTH, LEQ, GEQ, ADD, SUB, MUL, DIV, MOD };

        Oper oper;
        ImcExprPtr fst;
        ImcExprPtr snd;

        ImcBINOP(Oper oper, ImcExprPtr fst, ImcExprPtr snd)
            : oper(oper), fst(std::move(fst)), snd(std::move(snd)) {}

        static const char *operName(Oper oper) {
            switch (oper) {
                case Oper::OR: return "OR";
                case Oper::AND: return "AND";
                case Oper::EQU: return "EQU";
                case Oper::NEQ: return "NEQ";
                case Oper::LTH: return "LTH";
                case Oper::GTH: return "GTH";
                case Oper::LEQ: return "LEQ";
                case Oper::GEQ: return "GEQ";
                case Oper::ADD: return "ADD";
                case Oper::SUB: return "SUB";
                case Oper::MUL: return "MUL";
                case Oper::DIV: return "DIV";
                case Oper::MOD: return "MOD";
            }
            return "?";
        }

        void accept(ImcExprVisitor &v) override { v.visit(*this); }
        std::string toString() const override {
            return std::string(operName(oper)) + "(" + fst->toString() + ", " + snd->toString() + ")";
        }
    };
}
