//
// Created by bobi on 15. 9. 26.
//

#include "ImcLin/ExprCanonizer.h"

#include "ImcGen/data/expr/ImcBINOP.h"
#include "ImcGen/data/expr/ImcCALL.h"
#include "ImcGen/data/expr/ImcCONST.h"
#include "ImcGen/data/expr/ImcMEM.h"
#include "ImcGen/data/expr/ImcNAME.h"
#include "ImcGen/data/expr/ImcSEXPR.h"
#include "ImcGen/data/expr/ImcTEMP.h"
#include "ImcGen/data/expr/ImcUNOP.h"
#include "ImcGen/data/stmt/ImcMOVE.h"
#include "ImcLin/StmtCanonizer.h"

namespace Basic {
    ImcExprPtr ExprCanonizer::canonize(ImcExpr &expr) {
        expr.accept(*this);
        return std::move(m_result);
    }

    ImcExprPtr ExprCanonizer::toTemp(ImcExpr &expr) {
        if (auto *mem = dynamic_cast<ImcMEM *>(&expr); mem && mem->size > SLOT_SIZE) {
            ImcExprPtr addr = toTemp(*mem->addr);
            return std::make_unique<ImcMEM>(std::move(addr), mem->size);
        }

        ImcExprPtr value = canonize(expr);
        // Nothing that runs later can change a constant or a label, and a temp is
        // not reassigned once it is read: FP is fixed for the body, call results
        // and parked values each get a fresh one, and the result of && / || is
        // only reassigned inside its own SEXPR, before anything reads it.
        if (dynamic_cast<ImcCONST *>(value.get()) || dynamic_cast<ImcNAME *>(value.get())
            || dynamic_cast<ImcTEMP *>(value.get()))
            return value;

        const ImcTemp temp = ImcTemp::fresh();
        m_out.push_back(std::make_unique<ImcMOVE>(std::make_unique<ImcTEMP>(temp), std::move(value)));
        return std::make_unique<ImcTEMP>(temp);
    }

    void ExprCanonizer::visit(ImcCONST &e) { m_result = std::make_unique<ImcCONST>(e.value); }
    void ExprCanonizer::visit(ImcNAME &e) { m_result = std::make_unique<ImcNAME>(e.label); }
    void ExprCanonizer::visit(ImcTEMP &e) { m_result = std::make_unique<ImcTEMP>(e.temp); }

    void ExprCanonizer::visit(ImcMEM &e) {
        ImcExprPtr addr = canonize(*e.addr);
        m_result = std::make_unique<ImcMEM>(std::move(addr), e.size);
    }

    void ExprCanonizer::visit(ImcUNOP &e) {
        ImcExprPtr expr = canonize(*e.expr);
        m_result = std::make_unique<ImcUNOP>(e.oper, std::move(expr));
    }

    // Both operands go to temps, so a call in the second cannot disturb the first.
    void ExprCanonizer::visit(ImcBINOP &e) {
        ImcExprPtr fst = toTemp(*e.fst);
        ImcExprPtr snd = toTemp(*e.snd);
        m_result = std::make_unique<ImcBINOP>(e.oper, std::move(fst), std::move(snd));
    }

    void ExprCanonizer::visit(ImcCALL &e) {
        auto call = std::make_unique<ImcCALL>(e.label);
        for (std::size_t i = 0; i < e.args.size(); ++i)
            call->addArg(e.offsets[i], e.sizes[i], toTemp(*e.args[i]));

        // The result goes to a temp at once: the next call would overwrite RV.
        const ImcTemp result = ImcTemp::fresh();
        m_out.push_back(std::make_unique<ImcMOVE>(std::make_unique<ImcTEMP>(result), std::move(call)));
        m_result = std::make_unique<ImcTEMP>(result);
    }

    void ExprCanonizer::visit(ImcSEXPR &e) {
        for (ImcStmtPtr &s : StmtCanonizer().canonize(*e.stmt))
            m_out.push_back(std::move(s));
        m_result = canonize(*e.expr);
    }
}
