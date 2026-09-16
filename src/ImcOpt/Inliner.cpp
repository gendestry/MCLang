//
// Created by bobi on 16. 9. 26.
//

#include "ImcOpt/Inliner.h"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>

#include "ImcGen/data/expr/ImcCONST.h"
#include "ImcOpt/ImcClone.h"
#include "ImcOpt/ImcWalk.h"
#include "Mem.h"

namespace Basic {
    namespace {
        struct Candidate {
            const ImcGen::Function *function;
            std::map<long long, ImcTemp> params;   // argument offset -> the temp the body reads
            std::vector<const ImcStmt *> body;     // everything after the parameter loads
        };

        // MOVE(TEMP(p), MEM8(ADD(TEMP(FP), CONST(offset)))), offset > 0: a load Promoter put in.
        std::optional<std::pair<long long, ImcTemp>> paramLoad(const ImcStmt &stmt) {
            const auto *move = dynamic_cast<const ImcMOVE *>(&stmt);
            const auto *temp = move ? dynamic_cast<const ImcTEMP *>(move->dst.get()) : nullptr;
            const auto *mem = move ? dynamic_cast<const ImcMEM *>(move->src.get()) : nullptr;
            const auto *add = mem ? dynamic_cast<const ImcBINOP *>(mem->addr.get()) : nullptr;
            if (!temp || !mem || mem->size != SLOT_SIZE || !add || add->oper != ImcBINOP::Oper::ADD)
                return std::nullopt;
            const auto *fp = dynamic_cast<const ImcTEMP *>(add->fst.get());
            const auto *offset = dynamic_cast<const ImcCONST *>(add->snd.get());
            if (!fp || fp->temp != ImcTemp::FP() || !offset || offset->value <= 0)
                return std::nullopt;
            return std::make_pair(static_cast<long long>(offset->value), temp->temp);
        }


        std::size_t size(const ImcExpr &expr) {
            std::size_t n = 0;
            ImcWalk::any(expr, [&](const ImcExpr &) { ++n; return false; });
            return n;
        }

        // Whether anything in the statement calls or mentions FP; counts nodes on the way.
        bool usable(const ImcStmt &stmt, std::size_t &nodes) {
            auto check = [&](const ImcExpr &e) {
                nodes += size(e);
                return !ImcWalk::hasCall(e) && !ImcWalk::any(e, [](const ImcExpr &x) {
                    const auto *t = dynamic_cast<const ImcTEMP *>(&x);
                    return t && t->temp == ImcTemp::FP();
                });
            };
            ++nodes;
            if (const auto *move = dynamic_cast<const ImcMOVE *>(&stmt))
                return check(*move->dst) && check(*move->src);
            if (const auto *estmt = dynamic_cast<const ImcESTMT *>(&stmt))
                return check(*estmt->expr);
            if (const auto *cjump = dynamic_cast<const ImcCJUMP *>(&stmt))
                return check(*cjump->cond);
            if (const auto *cmd = dynamic_cast<const ImcCMD *>(&stmt)) {
                for (const ImcExprPtr &arg : cmd->args)
                    if (!check(*arg))
                        return false;
                return true;
            }
            if (const auto *stmts = dynamic_cast<const ImcSTMTS *>(&stmt)) {
                for (const ImcStmtPtr &s : stmts->stmts)
                    if (!usable(*s, nodes))
                        return false;
                return true;
            }
            return true; // JUMP, LABEL
        }

        std::optional<Candidate> candidate(const ImcGen::Function &f) {
            const auto *top = dynamic_cast<const ImcSTMTS *>(f.body.get());
            if (!top)
                return std::nullopt;
            // Promoter puts its loads (and local inits) at the top level, in front of
            // the original body; nothing else there reads a parameter slot.
            Candidate c{&f, {}, {}};
            std::size_t nodes = 0;
            for (std::size_t i = 0; i < top->stmts.size(); ++i) {
                if (const auto load = paramLoad(*top->stmts[i])) {
                    c.params.insert(*load);
                    continue;
                }
                if (!usable(*top->stmts[i], nodes) || nodes > Inliner::MAX_NODES)
                    return std::nullopt;
                c.body.push_back(top->stmts[i].get());
            }
            return c;
        }

        struct Inline {
            const std::unordered_map<std::string, Candidate> &candidates;
            const ImcGen::Function &into;
            std::size_t count = 0;

            ImcExprPtr expand(ImcCALL &call, const Candidate &c) {
                ImcRenaming renaming;
                const ImcTemp result = ImcTemp::fresh();
                const ImcLabel end = ImcLabel::fresh();
                renaming.rv = result;
                renaming.labels.emplace(c.function->exit.name, end);

                auto stmts = std::make_unique<ImcSTMTS>();
                stmts->add(std::make_unique<ImcMOVE>(std::make_unique<ImcTEMP>(result),
                                                     std::make_unique<ImcCONST>(0)));
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (call.offsets[i] == 0)
                        continue; // the static link: no body reads it
                    const ImcTemp arg = ImcTemp::fresh();
                    if (auto it = c.params.find(static_cast<long long>(call.offsets[i])); it != c.params.end())
                        renaming.temps.emplace(it->second.id, arg);
                    stmts->add(std::make_unique<ImcMOVE>(std::make_unique<ImcTEMP>(arg), std::move(call.args[i])));
                }
                for (const ImcStmt *s : c.body)
                    stmts->add(clone(*s, &renaming));
                stmts->add(std::make_unique<ImcLABEL>(end));
                return std::make_unique<ImcSEXPR>(std::move(stmts), std::make_unique<ImcTEMP>(result));
            }

            void expr(ImcExprPtr &slot) {
                if (auto *mem = dynamic_cast<ImcMEM *>(slot.get())) {
                    expr(mem->addr);
                } else if (auto *unop = dynamic_cast<ImcUNOP *>(slot.get())) {
                    expr(unop->expr);
                } else if (auto *binop = dynamic_cast<ImcBINOP *>(slot.get())) {
                    expr(binop->fst);
                    expr(binop->snd);
                } else if (auto *sexpr = dynamic_cast<ImcSEXPR *>(slot.get())) {
                    stmt(*sexpr->stmt);
                    expr(sexpr->expr);
                } else if (auto *call = dynamic_cast<ImcCALL *>(slot.get())) {
                    for (ImcExprPtr &arg : call->args)
                        expr(arg);
                    auto it = candidates.find(call->label.name);
                    if (it == candidates.end() || it->second.function == &into)
                        return;
                    for (std::size_t i = 0; i < call->args.size(); ++i)
                        if (call->sizes[i] != SLOT_SIZE)
                            return;
                    slot = expand(*call, it->second);
                    ++count;
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

    std::size_t Inliner::run(std::vector<ImcGen::Function> &functions) {
        std::unordered_map<std::string, Candidate> candidates;
        for (const ImcGen::Function &f : functions)
            if (std::optional<Candidate> c = candidate(f))
                candidates.emplace(f.frame->label, std::move(*c));

        std::size_t count = 0;
        for (ImcGen::Function &f : functions) {
            if (!f.body || candidates.contains(f.frame->label))
                continue; // a leaf has no calls to inline, and its body is being copied from
            Inline pass{candidates, f};
            pass.stmt(*f.body);
            count += pass.count;
        }
        return count;
    }
}
