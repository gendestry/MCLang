//
// Created by bobi on 15. 9. 26.
//
//  A call. Each argument is written at its offset in the outgoing argument
//  block; offset 0 is the static link. `sizes` says how many bytes each one
//  takes, so a record argument is copied whole.

#pragma once
#include <cstddef>
#include <vector>
#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/names/ImcLabel.h"

namespace Basic {
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

        void accept(ImcExprVisitor &v) override { v.visit(*this); }
        std::string toString() const override {
            std::string out = "CALL(" + label.name;
            for (std::size_t i = 0; i < args.size(); ++i)
                out += ", @" + std::to_string(offsets[i]) + ":" + args[i]->toString();
            return out + ")";
        }
    };
}
