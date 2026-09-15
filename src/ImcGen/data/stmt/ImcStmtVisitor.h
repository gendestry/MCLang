//
// Created by bobi on 15. 9. 26.
//
#pragma once

namespace Basic {
    struct ImcMOVE;
    struct ImcESTMT;
    struct ImcJUMP;
    struct ImcCJUMP;
    struct ImcLABEL;
    struct ImcSTMTS;

    struct ImcStmtVisitor {
        virtual ~ImcStmtVisitor() = default;
        virtual void visit(ImcMOVE &) = 0;
        virtual void visit(ImcESTMT &) = 0;
        virtual void visit(ImcJUMP &) = 0;
        virtual void visit(ImcCJUMP &) = 0;
        virtual void visit(ImcLABEL &) = 0;
        virtual void visit(ImcSTMTS &) = 0;
    };
}
