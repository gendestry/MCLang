//
// Created by bobi on 16. 9. 26.
//
//  Small helpers the optimization passes share: looking inside expressions,
//  rewriting them in place, and the control-flow facts of a linearized statement.

#pragma once
#include <functional>

#include "ImcGen/data/expr/ImcBINOP.h"
#include "ImcGen/data/expr/ImcCALL.h"
#include "ImcGen/data/expr/ImcMEM.h"
#include "ImcGen/data/expr/ImcSEXPR.h"
#include "ImcGen/data/expr/ImcTEMP.h"
#include "ImcGen/data/expr/ImcUNOP.h"
#include "ImcGen/data/stmt/ImcCJUMP.h"
#include "ImcGen/data/stmt/ImcCMD.h"
#include "ImcGen/data/stmt/ImcESTMT.h"
#include "ImcGen/data/stmt/ImcJUMP.h"
#include "ImcGen/data/stmt/ImcLABEL.h"
#include "ImcGen/data/stmt/ImcMOVE.h"
#include "ImcGen/data/stmt/ImcSTMTS.h"

namespace Basic::ImcWalk {
    // Whether `pred` holds for the expression or anything inside it. Statements
    // inside an SEXPR are not looked into.
    inline bool any(const ImcExpr &expr, const std::function<bool(const ImcExpr &)> &pred) {
        if (pred(expr))
            return true;
        if (const auto *mem = dynamic_cast<const ImcMEM *>(&expr))
            return any(*mem->addr, pred);
        if (const auto *unop = dynamic_cast<const ImcUNOP *>(&expr))
            return any(*unop->expr, pred);
        if (const auto *binop = dynamic_cast<const ImcBINOP *>(&expr))
            return any(*binop->fst, pred) || any(*binop->snd, pred);
        if (const auto *call = dynamic_cast<const ImcCALL *>(&expr)) {
            for (const ImcExprPtr &arg : call->args)
                if (any(*arg, pred))
                    return true;
            return false;
        }
        if (const auto *sexpr = dynamic_cast<const ImcSEXPR *>(&expr))
            return any(*sexpr->expr, pred);
        return false;
    }

    // A call, or an SEXPR that may hold one: dropping or moving it could change
    // what the program does.
    inline bool hasCall(const ImcExpr &expr) {
        return any(expr, [](const ImcExpr &e) {
            return dynamic_cast<const ImcCALL *>(&e) || dynamic_cast<const ImcSEXPR *>(&e);
        });
    }

    inline bool hasMem(const ImcExpr &expr) {
        return any(expr, [](const ImcExpr &e) { return dynamic_cast<const ImcMEM *>(&e) != nullptr; });
    }

    // Calls `f` on the slot, then on every slot inside what is there afterwards,
    // so `f` may replace the node it is given.
    inline void forEachSlot(ImcExprPtr &slot, const std::function<void(ImcExprPtr &)> &f) {
        f(slot);
        if (auto *mem = dynamic_cast<ImcMEM *>(slot.get())) {
            forEachSlot(mem->addr, f);
        } else if (auto *unop = dynamic_cast<ImcUNOP *>(slot.get())) {
            forEachSlot(unop->expr, f);
        } else if (auto *binop = dynamic_cast<ImcBINOP *>(slot.get())) {
            forEachSlot(binop->fst, f);
            forEachSlot(binop->snd, f);
        } else if (auto *call = dynamic_cast<ImcCALL *>(slot.get())) {
            for (ImcExprPtr &arg : call->args)
                forEachSlot(arg, f);
        } else if (auto *sexpr = dynamic_cast<ImcSEXPR *>(slot.get())) {
            forEachSlot(sexpr->expr, f);
        }
    }

    // Every expression a linearized statement reads. The TEMP a MOVE writes is not
    // read; the address of a MEM it writes is.
    inline void forEachRead(ImcStmt &stmt, const std::function<void(ImcExprPtr &)> &f) {
        if (auto *move = dynamic_cast<ImcMOVE *>(&stmt)) {
            if (auto *mem = dynamic_cast<ImcMEM *>(move->dst.get()))
                forEachSlot(mem->addr, f);
            forEachSlot(move->src, f);
        } else if (auto *estmt = dynamic_cast<ImcESTMT *>(&stmt)) {
            forEachSlot(estmt->expr, f);
        } else if (auto *cjump = dynamic_cast<ImcCJUMP *>(&stmt)) {
            forEachSlot(cjump->cond, f);
        } else if (auto *cmd = dynamic_cast<ImcCMD *>(&stmt)) {
            for (ImcExprPtr &arg : cmd->args)
                forEachSlot(arg, f);
        }
    }

    // A jump never falls through to the next statement (a CJUMP always jumps).
    inline bool isTerminator(const ImcStmt &stmt) {
        return dynamic_cast<const ImcJUMP *>(&stmt) || dynamic_cast<const ImcCJUMP *>(&stmt);
    }

    inline const ImcLABEL *asLabel(const ImcStmt &stmt) { return dynamic_cast<const ImcLABEL *>(&stmt); }

    inline void forEachTarget(ImcStmt &stmt, const std::function<void(ImcLabel &)> &f) {
        if (auto *jump = dynamic_cast<ImcJUMP *>(&stmt)) {
            f(jump->label);
        } else if (auto *cjump = dynamic_cast<ImcCJUMP *>(&stmt)) {
            f(cjump->pos);
            f(cjump->neg);
        }
    }

    // The TEMP a MOVE writes, or null.
    inline const ImcTEMP *movedTemp(const ImcStmt &stmt) {
        const auto *move = dynamic_cast<const ImcMOVE *>(&stmt);
        return move ? dynamic_cast<const ImcTEMP *>(move->dst.get()) : nullptr;
    }

    // Whether the statement may write memory: a store, a call (the callee can
    // write anything) or a raw command (which could run `data modify`).
    inline bool writesMemory(const ImcStmt &stmt) {
        if (auto *move = dynamic_cast<const ImcMOVE *>(&stmt))
            return dynamic_cast<const ImcMEM *>(move->dst.get()) || hasCall(*move->src);
        if (auto *estmt = dynamic_cast<const ImcESTMT *>(&stmt))
            return hasCall(*estmt->expr);
        return dynamic_cast<const ImcCMD *>(&stmt) != nullptr;
    }
}
