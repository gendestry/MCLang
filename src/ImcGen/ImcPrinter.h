//
// Created by bobi on 15. 9. 26.
//
//  Debug dump of an intermediate code tree, one node per line, children
//  indented under their parent. STMTS draws no line of its own: its statements
//  print at its depth, so a body reads as a flat run of MOVEs and jumps.

#pragma once
#include <cstddef>
#include <iostream>
#include <string>

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
#include "Utils/Colors/Font.h"

namespace Basic {
    class ImcPrinter : public ImcExprVisitor, public ImcStmtVisitor {
    public:
        explicit ImcPrinter(std::size_t depth = 0) : m_depth(depth) {}

        void print(ImcStmt &stmt) { stmt.accept(*this); }
        void print(ImcExpr &expr) { expr.accept(*this); }

        // ---- expressions ----
        void visit(ImcCONST &e) override {
            const std::string text = e.toString(); // "CONST(8)"
            line("CONST", text.substr(6, text.size() - 7));
        }
        void visit(ImcNAME &e) override { line("NAME", e.label.name); }
        void visit(ImcTEMP &e) override { line("TEMP", e.temp.toString()); }

        void visit(ImcMEM &e) override {
            line("MEM", std::to_string(e.size) + "B");
            nested(*e.addr);
        }

        void visit(ImcUNOP &e) override {
            line(e.oper == ImcUNOP::Oper::NEG ? "NEG" : "NOT");
            nested(*e.expr);
        }

        void visit(ImcBINOP &e) override {
            line(ImcBINOP::operName(e.oper));
            nested(*e.fst);
            nested(*e.snd);
        }

        void visit(ImcCALL &e) override {
            line("CALL", e.label.name);
            ++m_depth;
            for (std::size_t i = 0; i < e.args.size(); ++i) {
                line("arg", "@" + std::to_string(e.offsets[i]) + " " + std::to_string(e.sizes[i]) + "B");
                nested(*e.args[i]);
            }
            --m_depth;
        }

        void visit(ImcSEXPR &e) override {
            line("SEXPR");
            nested(*e.stmt);
            nested(*e.expr);
        }

        // ---- statements ----
        void visit(ImcMOVE &s) override {
            line("MOVE");
            nested(*s.dst);
            nested(*s.src);
        }

        void visit(ImcESTMT &s) override {
            line("ESTMT");
            nested(*s.expr);
        }

        void visit(ImcCMD &s) override {
            line("CMD", '"' + s.text + '"');
            for (ImcExprPtr &arg : s.args)
                nested(*arg);
        }

        void visit(ImcJUMP &s) override { line("JUMP", s.label.name); }

        void visit(ImcCJUMP &s) override {
            line("CJUMP", s.pos.name + " else " + s.neg.name);
            nested(*s.cond);
        }

        void visit(ImcLABEL &s) override { line("LABEL", s.label.name); }

        void visit(ImcSTMTS &s) override {
            for (ImcStmtPtr &stmt : s.stmts)
                stmt->accept(*this);
        }

    private:
        template <typename T>
        void nested(T &node) {
            ++m_depth;
            node.accept(*this);
            --m_depth;
        }

        void line(const std::string &kind, const std::string &detail = "") const {
            std::cout << std::string(m_depth * 2, ' ') << Utils::Font::colorMagenta << kind
                      << Utils::Font::colorReset;
            if (!detail.empty())
                std::cout << " " << Utils::Font::colorYellow << detail << Utils::Font::colorReset;
            std::cout << std::endl;
        }

        std::size_t m_depth;
    };
}
