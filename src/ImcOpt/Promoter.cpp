//
// Created by bobi on 16. 9. 26.
//

#include "ImcOpt/Promoter.h"

#include <map>
#include <optional>
#include <set>

#include "ImcGen/data/expr/ImcCONST.h"
#include "ImcOpt/ImcWalk.h"
#include "Mem.h"

namespace Basic {
    namespace {
        // ADD(TEMP(FP), CONST(offset)): the address of a slot in this frame.
        std::optional<long long> frameSlot(const ImcExpr &expr) {
            const auto *add = dynamic_cast<const ImcBINOP *>(&expr);
            if (!add || add->oper != ImcBINOP::Oper::ADD)
                return std::nullopt;
            const auto *fp = dynamic_cast<const ImcTEMP *>(add->fst.get());
            const auto *offset = dynamic_cast<const ImcCONST *>(add->snd.get());
            if (!fp || !offset || fp->temp != ImcTemp::FP())
                return std::nullopt;
            return static_cast<long long>(offset->value);
        }

        // Which slots are only read and written one slot at a time, and which are
        // used some other way.
        struct Scan {
            std::set<long long> accessed, escaped;

            void expr(const ImcExpr &e, bool slotAccess) {
                if (const std::optional<long long> slot = frameSlot(e)) {
                    (slotAccess ? accessed : escaped).insert(*slot);
                    return;
                }
                if (const auto *mem = dynamic_cast<const ImcMEM *>(&e)) {
                    expr(*mem->addr, mem->size == SLOT_SIZE);
                } else if (const auto *unop = dynamic_cast<const ImcUNOP *>(&e)) {
                    expr(*unop->expr, false);
                } else if (const auto *binop = dynamic_cast<const ImcBINOP *>(&e)) {
                    expr(*binop->fst, false);
                    expr(*binop->snd, false);
                } else if (const auto *call = dynamic_cast<const ImcCALL *>(&e)) {
                    for (const ImcExprPtr &arg : call->args)
                        expr(*arg, false);
                } else if (const auto *sexpr = dynamic_cast<const ImcSEXPR *>(&e)) {
                    stmt(*sexpr->stmt);
                    expr(*sexpr->expr, false);
                }
            }

            void stmt(const ImcStmt &s) {
                if (const auto *move = dynamic_cast<const ImcMOVE *>(&s)) {
                    expr(*move->dst, false);
                    expr(*move->src, false);
                } else if (const auto *estmt = dynamic_cast<const ImcESTMT *>(&s)) {
                    expr(*estmt->expr, false);
                } else if (const auto *cjump = dynamic_cast<const ImcCJUMP *>(&s)) {
                    expr(*cjump->cond, false);
                } else if (const auto *cmd = dynamic_cast<const ImcCMD *>(&s)) {
                    for (const ImcExprPtr &arg : cmd->args)
                        expr(*arg, false);
                } else if (const auto *stmts = dynamic_cast<const ImcSTMTS *>(&s)) {
                    for (const ImcStmtPtr &child : stmts->stmts)
                        stmt(*child);
                }
            }
        };

        struct Rewrite {
            const std::map<long long, ImcTemp> &temps;

            void expr(ImcExprPtr &slot) {
                if (auto *mem = dynamic_cast<ImcMEM *>(slot.get())) {
                    if (const std::optional<long long> at = frameSlot(*mem->addr); at && mem->size == SLOT_SIZE) {
                        if (auto it = temps.find(*at); it != temps.end()) {
                            slot = std::make_unique<ImcTEMP>(it->second);
                            return;
                        }
                    }
                    expr(mem->addr);
                } else if (auto *unop = dynamic_cast<ImcUNOP *>(slot.get())) {
                    expr(unop->expr);
                } else if (auto *binop = dynamic_cast<ImcBINOP *>(slot.get())) {
                    expr(binop->fst);
                    expr(binop->snd);
                } else if (auto *call = dynamic_cast<ImcCALL *>(slot.get())) {
                    for (ImcExprPtr &arg : call->args)
                        expr(arg);
                } else if (auto *sexpr = dynamic_cast<ImcSEXPR *>(slot.get())) {
                    stmt(*sexpr->stmt);
                    expr(sexpr->expr);
                }
            }

            void stmt(ImcStmt &s) {
                if (auto *move = dynamic_cast<ImcMOVE *>(&s)) {
                    expr(move->dst);
                    expr(move->src);
                } else if (auto *estmt = dynamic_cast<ImcESTMT *>(&s)) {
                    expr(estmt->expr);
                } else if (auto *cjump = dynamic_cast<ImcCJUMP *>(&s)) {
                    expr(cjump->cond);
                } else if (auto *cmd = dynamic_cast<ImcCMD *>(&s)) {
                    for (ImcExprPtr &arg : cmd->args)
                        expr(arg);
                } else if (auto *stmts = dynamic_cast<ImcSTMTS *>(&s)) {
                    for (ImcStmtPtr &child : stmts->stmts)
                        stmt(*child);
                }
            }
        };
    }

    std::size_t Promoter::run(ImcStmtPtr &body) {
        if (!body)
            return 0;

        Scan scan;
        scan.stmt(*body);
        std::map<long long, ImcTemp> temps;
        for (long long slot : scan.accessed)
            if (!scan.escaped.contains(slot))
                temps.emplace(slot, ImcTemp::fresh());
        if (temps.empty())
            return 0;

        Rewrite{temps}.stmt(*body);

        // Parameters sit above FP, locals below it.
        auto stmts = std::make_unique<ImcSTMTS>();
        for (const auto &[slot, temp] : temps) {
            ImcExprPtr init = slot > 0
                                  ? ImcExprPtr(std::make_unique<ImcMEM>(std::make_unique<ImcBINOP>(
                                        ImcBINOP::Oper::ADD, std::make_unique<ImcTEMP>(ImcTemp::FP()),
                                        std::make_unique<ImcCONST>(static_cast<double>(slot)))))
                                  : ImcExprPtr(std::make_unique<ImcCONST>(0));
            stmts->add(std::make_unique<ImcMOVE>(std::make_unique<ImcTEMP>(temp), std::move(init)));
        }
        stmts->add(std::move(body));
        body = std::move(stmts);
        return temps.size();
    }
}
