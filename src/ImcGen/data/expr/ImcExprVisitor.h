//
// Created by bobi on 15. 9. 26.
//
#pragma once

namespace Basic {
    struct ImcCONST;
    struct ImcNAME;
    struct ImcTEMP;
    struct ImcMEM;
    struct ImcUNOP;
    struct ImcBINOP;
    struct ImcCALL;
    struct ImcSEXPR;

    struct ImcExprVisitor {
        virtual ~ImcExprVisitor() = default;
        virtual void visit(ImcCONST &) = 0;
        virtual void visit(ImcNAME &) = 0;
        virtual void visit(ImcTEMP &) = 0;
        virtual void visit(ImcMEM &) = 0;
        virtual void visit(ImcUNOP &) = 0;
        virtual void visit(ImcBINOP &) = 0;
        virtual void visit(ImcCALL &) = 0;
        virtual void visit(ImcSEXPR &) = 0;
    };
}
