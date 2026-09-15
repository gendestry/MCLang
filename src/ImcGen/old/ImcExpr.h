//
// Created by bobi on 15. 9. 26.
//
//  Intermediate code expressions: trees that compute a value. Every child is
//  owned by its parent, so a whole tree goes away with its root.
//
//  ImcSEXPR (a statement followed by a value) lives in ImcStmt.h, because it has
//  to own a statement.

#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Mem.h"
#include "ImcNames.h"

namespace Basic {
    struct ImcExpr {
        virtual ~ImcExpr() = default;
        virtual std::string toString() const = 0;
    };

    using ImcExprPtr = std::unique_ptr<ImcExpr>;

    // A constant: numbers, and booleans as 0 or 1.
    struct ImcCONST : ImcExpr {
        long long value = 0;

        explicit ImcCONST(long long value) : value(value) {}

        std::string toString() const override { return "CONST(" + std::to_string(value) + ")"; }
    };

    // The address a label stands for (a global, a string literal, a function).
    struct ImcNAME : ImcExpr {
        ImcLabel label;

        explicit ImcNAME(ImcLabel label) : label(std::move(label)) {}

        std::string toString() const override { return "NAME(" + label.name + ")"; }
    };

    // The value held in a temporary.
    struct ImcTEMP : ImcExpr {
        ImcTemp temp;

        explicit ImcTEMP(ImcTemp temp) : temp(temp) {}

        std::string toString() const override { return "TEMP(" + temp.toString() + ")"; }
    };

    // The contents of memory at an address. `size` is how many bytes are read:
    // one slot for scalars, the whole layout for a record value.
    struct ImcMEM : ImcExpr {
        ImcExprPtr addr;
        std::size_t size = SLOT_SIZE;

        explicit ImcMEM(ImcExprPtr addr, std::size_t size = SLOT_SIZE)
            : addr(std::move(addr)), size(size) {}

        std::string toString() const override {
            return "MEM" + std::to_string(size) + "(" + addr->toString() + ")";
        }
    };

    struct ImcUNOP : ImcExpr {
        enum class Oper { NEG, NOT };

        Oper oper;
        ImcExprPtr expr;

        ImcUNOP(Oper oper, ImcExprPtr expr) : oper(oper), expr(std::move(expr)) {}

        std::string toString() const override {
            return std::string(oper == Oper::NEG ? "NEG" : "NOT") + "(" + expr->toString() + ")";
        }
    };

    // AND and OR here evaluate both sides; short-circuiting is done with jumps.
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

        std::string toString() const override {
            return std::string(operName(oper)) + "(" + fst->toString() + ", " + snd->toString() + ")";
        }
    };

    // A call. Each argument is written at its offset in the outgoing argument
    // block; offset 0 is the static link. `sizes` says how many bytes each one
    // takes, so a record argument is copied whole.
    struct ImcCALL : ImcExpr {
        ImcLabel label;
        std::vector<std::size_t> offsets;
        std::vector<std::size_t> sizes;
        std::vector<ImcExprPtr> args;

        explicit ImcCALL(ImcLabel label) : label(std::move(label)) {}

        void addArg(std::size_t offset, std::size_t size, ImcExprPtr arg) {
            offsets.push_back(offset);
            sizes.push_back(size);
            args.push_back(std::move(arg));
        }

        std::string toString() const override {
            std::string out = "CALL(" + label.name;
            for (std::size_t i = 0; i < args.size(); ++i)
                out += ", @" + std::to_string(offsets[i]) + ":" + args[i]->toString();
            return out + ")";
        }
    };
}
