//
// Created by bobi on 15. 9. 26.
//
//  The contents of memory at an address. `size` is how many bytes are read:
//  one slot for scalars, the whole layout for a record value.

#pragma once
#include <cstddef>
#include "ImcGen/data/expr/ImcExpr.h"
#include "Mem.h"

namespace Basic {
    struct ImcMEM : ImcExpr {
        ImcExprPtr addr;
        std::size_t size = SLOT_SIZE;

        explicit ImcMEM(ImcExprPtr addr, std::size_t size = SLOT_SIZE)
            : addr(std::move(addr)), size(size) {}

        void accept(ImcExprVisitor &v) override { v.visit(*this); }
        std::string toString() const override {
            return "MEM" + std::to_string(size) + "(" + addr->toString() + ")";
        }
    };
}
