//
// Created by bobi on 15. 9. 26.
//
//  A tree that computes a value. Every child is owned by its parent, so a whole
//  tree goes away with its root.

#pragma once
#include <memory>
#include <string>
#include "ImcGen/data/expr/ImcExprVisitor.h"

namespace Basic {
    struct ImcExpr {
        virtual ~ImcExpr() = default;
        virtual void accept(ImcExprVisitor &v) = 0;
        virtual std::string toString() const = 0;
    };

    using ImcExprPtr = std::unique_ptr<ImcExpr>;
}
