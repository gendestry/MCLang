//
// Created by bobi on 16. 9. 26.
//

#include "ImcOpt/Liveness.h"

#include <string>
#include <unordered_map>

#include "ImcGen/data/expr/ImcBINOP.h"
#include "ImcGen/data/expr/ImcCALL.h"
#include "ImcGen/data/expr/ImcMEM.h"
#include "ImcGen/data/expr/ImcSEXPR.h"
#include "ImcGen/data/expr/ImcTEMP.h"
#include "ImcGen/data/expr/ImcUNOP.h"
#include "ImcGen/data/stmt/ImcCJUMP.h"
#include "ImcGen/data/stmt/ImcESTMT.h"
#include "ImcGen/data/stmt/ImcJUMP.h"
#include "ImcGen/data/stmt/ImcLABEL.h"
#include "ImcGen/data/stmt/ImcMOVE.h"

namespace Basic {
    namespace {
        bool tracked(const ImcTemp &temp) { return temp.id > ImcTemp::RV().id; }

        void collect(const ImcExpr &expr, Liveness::Temps &into) {
            if (const auto *temp = dynamic_cast<const ImcTEMP *>(&expr)) {
                if (tracked(temp->temp))
                    into.insert(temp->temp.id);
            } else if (const auto *mem = dynamic_cast<const ImcMEM *>(&expr)) {
                collect(*mem->addr, into);
            } else if (const auto *unop = dynamic_cast<const ImcUNOP *>(&expr)) {
                collect(*unop->expr, into);
            } else if (const auto *binop = dynamic_cast<const ImcBINOP *>(&expr)) {
                collect(*binop->fst, into);
                collect(*binop->snd, into);
            } else if (const auto *call = dynamic_cast<const ImcCALL *>(&expr)) {
                for (const ImcExprPtr &arg : call->args)
                    collect(*arg, into);
            } else if (const auto *sexpr = dynamic_cast<const ImcSEXPR *>(&expr)) {
                // Not in linearized code; counted as a read so nothing is missed.
                collect(*sexpr->expr, into);
            }
        }
    }

    Liveness::Temps Liveness::uses(const ImcStmt &stmt) {
        Temps out;
        if (const auto *move = dynamic_cast<const ImcMOVE *>(&stmt)) {
            // Writing a temp doesn't read it; writing memory reads the address.
            if (const auto *mem = dynamic_cast<const ImcMEM *>(move->dst.get()))
                collect(*mem->addr, out);
            collect(*move->src, out);
        } else if (const auto *estmt = dynamic_cast<const ImcESTMT *>(&stmt)) {
            collect(*estmt->expr, out);
        } else if (const auto *cjump = dynamic_cast<const ImcCJUMP *>(&stmt)) {
            collect(*cjump->cond, out);
        }
        return out;
    }

    Liveness::Temps Liveness::defs(const ImcStmt &stmt) {
        Temps out;
        if (const auto *move = dynamic_cast<const ImcMOVE *>(&stmt))
            if (const auto *temp = dynamic_cast<const ImcTEMP *>(move->dst.get()); temp && tracked(temp->temp))
                out.insert(temp->temp.id);
        return out;
    }

    const ImcCALL *Liveness::callIn(const ImcStmt &stmt) {
        if (const auto *move = dynamic_cast<const ImcMOVE *>(&stmt))
            return dynamic_cast<const ImcCALL *>(move->src.get());
        if (const auto *estmt = dynamic_cast<const ImcESTMT *>(&stmt))
            return dynamic_cast<const ImcCALL *>(estmt->expr.get());
        return nullptr;
    }

    std::vector<Liveness::Temps> Liveness::liveOut(const LinCodeChunk &chunk) {
        const std::vector<ImcStmtPtr> &stmts = chunk.stmts;
        const std::size_t n = stmts.size();

        std::unordered_map<std::string, std::size_t> labels;
        for (std::size_t i = 0; i < n; ++i)
            if (const auto *label = dynamic_cast<const ImcLABEL *>(stmts[i].get()))
                labels[label->label.name] = i;

        std::vector<std::vector<std::size_t>> succ(n);
        std::vector<Temps> use(n), def(n);
        for (std::size_t i = 0; i < n; ++i) {
            const ImcStmt &s = *stmts[i];
            auto to = [&](const ImcLabel &label) {
                if (auto it = labels.find(label.name); it != labels.end())
                    succ[i].push_back(it->second);
            };
            if (const auto *jump = dynamic_cast<const ImcJUMP *>(&s)) {
                to(jump->label);
            } else if (const auto *cjump = dynamic_cast<const ImcCJUMP *>(&s)) {
                to(cjump->pos);
                to(cjump->neg);
            } else if (i + 1 < n) {
                succ[i].push_back(i + 1);
            }
            use[i] = uses(s);
            def[i] = defs(s);
        }

        std::vector<Temps> in(n), out(n);
        bool changed = true;
        while (changed) {
            changed = false;
            // Backwards, so most facts flow through in a single sweep.
            for (std::size_t i = n; i-- > 0;) {
                Temps newOut;
                for (std::size_t t : succ[i])
                    newOut.insert(in[t].begin(), in[t].end());

                Temps newIn = use[i];
                for (std::size_t temp : newOut)
                    if (!def[i].contains(temp))
                        newIn.insert(temp);

                if (newOut != out[i] || newIn != in[i]) {
                    out[i] = std::move(newOut);
                    in[i] = std::move(newIn);
                    changed = true;
                }
            }
        }
        return out;
    }
}
