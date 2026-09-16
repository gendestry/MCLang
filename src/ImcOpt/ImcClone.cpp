//
// Created by bobi on 16. 9. 26.
//

#include "ImcOpt/ImcClone.h"

#include <stdexcept>

#include "ImcGen/data/expr/ImcBINOP.h"
#include "ImcGen/data/expr/ImcCALL.h"
#include "ImcGen/data/expr/ImcCONST.h"
#include "ImcGen/data/expr/ImcMEM.h"
#include "ImcGen/data/expr/ImcNAME.h"
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

namespace Basic {
    namespace {
        ImcTemp mapTemp(const ImcTemp &temp, ImcRenaming *r) {
            if (!r)
                return temp;
            if (temp == ImcTemp::RV())
                return r->rv ? *r->rv : temp;
            if (temp == ImcTemp::FP())
                return temp;
            if (auto it = r->temps.find(temp.id); it != r->temps.end())
                return it->second;
            const ImcTemp fresh = ImcTemp::fresh();
            r->temps.emplace(temp.id, fresh);
            return fresh;
        }

        ImcLabel mapLabel(const ImcLabel &label, ImcRenaming *r) {
            if (!r)
                return label;
            if (auto it = r->labels.find(label.name); it != r->labels.end())
                return it->second;
            const ImcLabel fresh = ImcLabel::fresh();
            r->labels.emplace(label.name, fresh);
            return fresh;
        }
    }

    ImcExprPtr clone(const ImcExpr &expr, ImcRenaming *r) {
        if (const auto *c = dynamic_cast<const ImcCONST *>(&expr))
            return std::make_unique<ImcCONST>(c->value);
        if (const auto *name = dynamic_cast<const ImcNAME *>(&expr))
            return std::make_unique<ImcNAME>(name->label);
        if (const auto *temp = dynamic_cast<const ImcTEMP *>(&expr))
            return std::make_unique<ImcTEMP>(mapTemp(temp->temp, r));
        if (const auto *mem = dynamic_cast<const ImcMEM *>(&expr))
            return std::make_unique<ImcMEM>(clone(*mem->addr, r), mem->size);
        if (const auto *unop = dynamic_cast<const ImcUNOP *>(&expr))
            return std::make_unique<ImcUNOP>(unop->oper, clone(*unop->expr, r));
        if (const auto *binop = dynamic_cast<const ImcBINOP *>(&expr))
            return std::make_unique<ImcBINOP>(binop->oper, clone(*binop->fst, r), clone(*binop->snd, r));
        if (const auto *call = dynamic_cast<const ImcCALL *>(&expr)) {
            auto copy = std::make_unique<ImcCALL>(call->label);
            for (std::size_t i = 0; i < call->args.size(); ++i)
                copy->addArg(call->offsets[i], call->sizes[i], clone(*call->args[i], r));
            return copy;
        }
        if (const auto *sexpr = dynamic_cast<const ImcSEXPR *>(&expr))
            return std::make_unique<ImcSEXPR>(clone(*sexpr->stmt, r), clone(*sexpr->expr, r));
        throw std::logic_error("clone: unknown expression " + expr.toString());
    }

    ImcStmtPtr clone(const ImcStmt &stmt, ImcRenaming *r) {
        if (const auto *move = dynamic_cast<const ImcMOVE *>(&stmt))
            return std::make_unique<ImcMOVE>(clone(*move->dst, r), clone(*move->src, r));
        if (const auto *estmt = dynamic_cast<const ImcESTMT *>(&stmt))
            return std::make_unique<ImcESTMT>(clone(*estmt->expr, r));
        if (const auto *jump = dynamic_cast<const ImcJUMP *>(&stmt))
            return std::make_unique<ImcJUMP>(mapLabel(jump->label, r));
        if (const auto *cjump = dynamic_cast<const ImcCJUMP *>(&stmt))
            return std::make_unique<ImcCJUMP>(clone(*cjump->cond, r), mapLabel(cjump->pos, r),
                                              mapLabel(cjump->neg, r));
        if (const auto *label = dynamic_cast<const ImcLABEL *>(&stmt))
            return std::make_unique<ImcLABEL>(mapLabel(label->label, r));
        if (const auto *stmts = dynamic_cast<const ImcSTMTS *>(&stmt)) {
            auto copy = std::make_unique<ImcSTMTS>();
            for (const ImcStmtPtr &s : stmts->stmts)
                copy->add(clone(*s, r));
            return copy;
        }
        if (const auto *cmd = dynamic_cast<const ImcCMD *>(&stmt)) {
            auto copy = cmd->dst ? std::make_unique<ImcCMD>(cmd->text, cmd->store, mapTemp(*cmd->dst, r))
                                 : std::make_unique<ImcCMD>(cmd->text);
            for (const ImcExprPtr &arg : cmd->args)
                copy->addArg(clone(*arg, r));
            return copy;
        }
        throw std::logic_error("clone: unknown statement " + stmt.toString());
    }
}
