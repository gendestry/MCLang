//
// Created by bobi on 15. 9. 26.
//
//  A tree that does something (move, jump) instead of computing a value.

#pragma once
#include <memory>
#include <string>
#include "ImcGen/data/stmt/ImcStmtVisitor.h"

namespace Basic {
    struct ImcStmt {
        virtual ~ImcStmt() = default;
        virtual void accept(ImcStmtVisitor &v) = 0;
        virtual std::string toString() const = 0;
    };

    using ImcStmtPtr = std::unique_ptr<ImcStmt>;
}
