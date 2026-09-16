//
// Created by bobi on 16. 9. 26.
//
//  A raw Minecraft command, written in the source as cmd("..."). `text` is the
//  command with one `{}` where each argument goes, and `args` are the values to
//  put there -- so cmd("setblock {} {} {} stone", x, y, z) keeps the command
//  intact and lets the machine fill in the coordinates.
//
//  It is a statement, so nothing about it is folded away or reordered: running
//  it is the point. When its value is wanted -- cmd(...) or cmdValue(...) used
//  in an expression -- `store` says which one, and `dst` is the temp it lands
//  in, in fixed point like every other number; ImcGen wraps the pair in an
//  SEXPR that reads `dst`.

#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/names/ImcTemp.h"
#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    struct ImcCMD : ImcStmt {
        // None: run it and drop the value. Success: 1 when it worked, else 0.
        // Result: the number the command reports (a count, a score, ...).
        enum class Store { None, Success, Result };

        std::string text;
        std::vector<ImcExprPtr> args;
        Store store = Store::None;
        std::optional<ImcTemp> dst; // set exactly when store is not None

        explicit ImcCMD(std::string text) : text(std::move(text)) {}
        ImcCMD(std::string text, Store store, ImcTemp dst) : text(std::move(text)), store(store), dst(dst) {}

        void addArg(ImcExprPtr arg) { args.push_back(std::move(arg)); }

        // How many `{}` holes the text has; the checker keeps it equal to args.
        static std::size_t holes(const std::string &text) {
            std::size_t count = 0;
            for (std::size_t i = 0; i + 1 < text.size(); ++i)
                if (text[i] == '{' && text[i + 1] == '}')
                    ++count, ++i;
            return count;
        }

        void accept(ImcStmtVisitor &v) override { v.visit(*this); }
        std::string toString() const override {
            std::string out = "CMD(\"" + text + "\"";
            for (const ImcExprPtr &arg : args)
                out += ", " + arg->toString();
            out += ")";
            if (dst)
                out += (store == Store::Success ? " success -> " : " result -> ") + dst->toString();
            return out;
        }
    };
}
