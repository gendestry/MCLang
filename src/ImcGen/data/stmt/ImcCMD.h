//
// Created by bobi on 16. 9. 26.
//
//  A raw Minecraft command, written in the source as cmd("..."). `text` is the
//  command with one `{}` where each argument goes, and `args` are the values to
//  put there -- so cmd("setblock {} {} {} stone", x, y, z) keeps the command
//  intact and lets the machine fill in the coordinates.
//
//  It is a statement, not an expression: a command has no value, and nothing
//  about it may be folded away or reordered, since running it is the point.

#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    struct ImcCMD : ImcStmt {
        std::string text;
        std::vector<ImcExprPtr> args;

        explicit ImcCMD(std::string text) : text(std::move(text)) {}

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
            return out + ")";
        }
    };
}
