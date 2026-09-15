//
// Created by bobi on 15. 9. 26.
//

#include "ImcLin/StmtCanonizer.h"

#include <stdexcept>

#include "ImcGen/data/expr/ImcCALL.h"
#include "ImcGen/data/expr/ImcMEM.h"
#include "ImcGen/data/expr/ImcTEMP.h"
#include "ImcGen/data/stmt/ImcCJUMP.h"
#include "ImcGen/data/stmt/ImcESTMT.h"
#include "ImcGen/data/stmt/ImcJUMP.h"
#include "ImcGen/data/stmt/ImcLABEL.h"
#include "ImcGen/data/stmt/ImcMOVE.h"
#include "ImcGen/data/stmt/ImcSTMTS.h"
#include "ImcLin/ExprCanonizer.h"

namespace Basic {
    // Swapping keeps this re-entrant: ExprCanonizer starts a fresh StmtCanonizer
    // for an SEXPR, but a nested canonize() on this one would also be safe.
    std::vector<ImcStmtPtr> StmtCanonizer::canonize(ImcStmt &stmt) {
        std::vector<ImcStmtPtr> out;
        std::swap(out, m_out);
        stmt.accept(*this);
        std::swap(out, m_out);
        return out;
    }

    void StmtCanonizer::visit(ImcMOVE &s) {
        ExprCanonizer exprs(m_out);

        if (auto *mem = dynamic_cast<ImcMEM *>(s.dst.get())) {
            // The address is worked out first, then the value, then one plain store.
            ImcExprPtr addr = exprs.toTemp(*mem->addr);
            ImcExprPtr src = exprs.toTemp(*s.src);
            m_out.push_back(std::make_unique<ImcMOVE>(std::make_unique<ImcMEM>(std::move(addr), mem->size),
                                                      std::move(src)));
            return;
        }

        if (auto *temp = dynamic_cast<ImcTEMP *>(s.dst.get())) {
            ImcExprPtr src = exprs.canonize(*s.src);
            m_out.push_back(std::make_unique<ImcMOVE>(std::make_unique<ImcTEMP>(temp->temp), std::move(src)));
            return;
        }

        throw std::runtime_error("canonizer: MOVE into neither a MEM nor a TEMP: " + s.toString());
    }

    void StmtCanonizer::visit(ImcESTMT &s) {
        ExprCanonizer exprs(m_out);

        if (auto *call = dynamic_cast<ImcCALL *>(s.expr.get())) {
            // The result is unused, so the call stays a statement instead of being
            // parked in a temp. Its arguments still go to temps, so a call inside
            // one argument cannot clobber another argument's value.
            auto flat = std::make_unique<ImcCALL>(call->label);
            for (std::size_t i = 0; i < call->args.size(); ++i)
                flat->addArg(call->offsets[i], call->sizes[i], exprs.toTemp(*call->args[i]));
            m_out.push_back(std::make_unique<ImcESTMT>(std::move(flat)));
            return;
        }

        ImcExprPtr expr = exprs.canonize(*s.expr);
        m_out.push_back(std::make_unique<ImcESTMT>(std::move(expr)));
    }

    void StmtCanonizer::visit(ImcJUMP &s) { m_out.push_back(std::make_unique<ImcJUMP>(s.label)); }
    void StmtCanonizer::visit(ImcLABEL &s) { m_out.push_back(std::make_unique<ImcLABEL>(s.label)); }

    void StmtCanonizer::visit(ImcCJUMP &s) {
        ImcExprPtr cond = ExprCanonizer(m_out).canonize(*s.cond);
        m_out.push_back(std::make_unique<ImcCJUMP>(std::move(cond), s.pos, s.neg));
    }

    void StmtCanonizer::visit(ImcSTMTS &s) {
        for (ImcStmtPtr &stmt : s.stmts)
            stmt->accept(*this);
    }
}
