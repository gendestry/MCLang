//
// Created by bobi on 6. 9. 26.
//
//  Debug dump of a built AST -- the visitor counterpart to
//  Parsing::Syntax::printTree, but over typed Basic:: nodes instead of the CST.
//
//  Drawing happens in two passes so the tree glyphs can be chosen correctly:
//  the visitors first lower the AST into a plain label/children `Row` tree
//  (a visitor cannot know whether it is the last child of its parent), then
//  render() walks that with the same connector + chain-collapsing rules the
//  CST printer uses, so both dumps read the same way.

#pragma once
#include <iostream>
#include <string>
#include <vector>

#include "LangAst.h"
#include "Utils/Colors/Font.h"

namespace Basic {
    class AstPrinter : public TypeVisitor,
                       public ExprVisitor,
                       public StmtVisitor,
                       public DeclVisitor {
    public:
        void print(const Program &program) {
            Row root{label("Program"), {}};
            for (const DeclPtr &d : program)
                add(root, d);
            render(root, "", true, true);
        }

        // ---- Type ----
        void visit(AtomicType &t) override { m_row = {label("AtomicType", t.prim), {}}; }
        void visit(NamedType &t) override { m_row = {label("NamedType", t.name), {}}; }

        // ---- Expr ----
        void visit(NumberExpr &e) override { m_row = {label("NumberExpr", trim(e.value)), {}}; }
        void visit(BoolExpr &e) override { m_row = {label("BoolExpr", e.value ? "true" : "false"), {}}; }

        void visit(BinaryExpr &e) override {
            Row r{label("BinaryExpr", e.op), {}};
            add(r, e.lhs);
            add(r, e.rhs);
            m_row = std::move(r);
        }

        void visit(CallExpr &e) override {
            Row r{label("CallExpr", e.callee), {}};
            for (const ExprPtr &a : e.args)
                add(r, a);
            m_row = std::move(r);
        }

        void visit(NamedExpr &e) override { m_row = {label("NamedExpr", e.callee), {}}; }

        void visit(UnaryExpr &e) override {
            Row r{label("UnaryExpr", e.op), {}};
            add(r, e.operand);
            m_row = std::move(r);
        }

        void visit(AccessExpr &e) override {
            Row r{label("AccessExpr", "." + e.member), {}};
            add(r, e.base);
            m_row = std::move(r);
        }

        // ---- Stmt ----
        void visit(CompoundStmt &s) override {
            Row r{label("CompoundStmt"), {}};
            for (const StmtPtr &b : s.body)
                add(r, b);
            m_row = std::move(r);
        }

        void visit(VarDeclStmt &s) override {
            Row r{label("VarDeclStmt"), {}};
            add(r, s.decl);
            m_row = std::move(r);
        }

        void visit(ExprStmt &s) override {
            Row r{label("ExprStmt"), {}};
            add(r, s.expr);
            m_row = std::move(r);
        }

        void visit(ReturnStmt &s) override {
            Row r{label("ReturnStmt"), {}};
            add(r, s.expr); // no child for a bare `return;`
            m_row = std::move(r);
        }

        void visit(AssignStmt &s) override {
            Row r{label("AssignStmt"), {}};
            add(r, s.target);
            add(r, s.value);
            m_row = std::move(r);
        }

        // Control flow carries several same-typed children, so each gets a named
        // group row -- otherwise `IfStmt` with three bare kids is unreadable.
        void visit(IfStmt &s) override {
            Row r{label("IfStmt"), {}};
            group(r, "cond", s.cond);
            group(r, "then", s.then);
            group(r, "else", s.otherwise); // omitted entirely when there is no else
            m_row = std::move(r);
        }

        void visit(WhileStmt &s) override {
            Row r{label("WhileStmt"), {}};
            group(r, "cond", s.cond);
            group(r, "body", s.body);
            m_row = std::move(r);
        }

        void visit(ForStmt &s) override {
            Row r{label("ForStmt"), {}};
            group(r, "init", s.init); // omitted for `for (; ...`
            group(r, "cond", s.cond);
            group(r, "step", s.step);
            group(r, "body", s.body);
            m_row = std::move(r);
        }

        // ---- Decl ----
        void visit(VarDecl &d) override {
            Row r{label("VarDecl", d.name), {}};
            add(r, d.type);
            add(r, d.init); // no child when there is no initialiser
            m_row = std::move(r);
        }

        void visit(FunDecl &d) override {
            Row r{label("FunDecl", d.name), {}};
            add(r, d.returnType);
            for (const Param &p : d.params) {
                Row param{label("Param", p.name), {}};
                add(param, p.type);
                r.kids.push_back(std::move(param));
            }
            add(r, d.body);
            m_row = std::move(r);
        }

        void visit(RecordDecl &d) override {
            Row r{label("RecordDecl", d.name), {}};
            for (const DeclPtr &f : d.fields)
                add(r, f);
            m_row = std::move(r);
        }

    private:
        // One already-coloured line plus its children -- the shape render() needs.
        struct Row {
            std::string label;
            std::vector<Row> kids;
        };

        // Visit `node` and append the Row it produced. Null children (an absent
        // initialiser, a bare return) contribute nothing.
        template <typename T>
        void add(Row &parent, const std::unique_ptr<T> &node) {
            if (!node)
                return;
            node->accept(*this); // the visit() overloads leave their Row in m_row
            parent.kids.push_back(std::move(m_row));
        }

        // A labelled wrapper row ("cond", "then", ...). Because render() collapses
        // single-child chains, this costs no extra line: `cond -> BinaryExpr '<'`.
        template <typename T>
        void group(Row &parent, const std::string &name, const std::unique_ptr<T> &node) {
            if (!node)
                return;
            node->accept(*this);
            Row g{Utils::Font::colorBlue + name + Utils::Font::colorReset, {}};
            g.kids.push_back(std::move(m_row));
            parent.kids.push_back(std::move(g));
        }

        static std::string label(const std::string &kind) {
            return Utils::Font::colorMagenta + kind + Utils::Font::colorReset;
        }

        // "Kind 'detail'" -- the detail is the name / operator / literal.
        static std::string label(const std::string &kind, const std::string &detail) {
            return label(kind) + " " + Utils::Font::colorYellow + "'" + detail + "'"
                   + Utils::Font::colorReset;
        }

        // `prefix` is the indentation drawn for this row's children; `isLast`
        // picks this row's own connector glyph.
        static void render(const Row &row, const std::string &prefix, bool isLast, bool isRoot) {
            std::string line;
            if (!isRoot)
                line += Utils::Font::colorDim + (isLast ? "└── " : "├── ") + Utils::Font::colorReset;

            // Collapse single-child chains (VarDecl -> AtomicType, ReturnStmt ->
            // NamedExpr, ...) onto one line so structure is not buried in indentation.
            const Row *cur = &row;
            line += cur->label;
            while (cur->kids.size() == 1) {
                cur = &cur->kids[0];
                line += Utils::Font::colorDim + " → " + Utils::Font::colorReset + cur->label;
            }
            std::cout << prefix << line << std::endl;

            const std::string childPrefix = prefix + (isRoot ? "" : (isLast ? "    " : "│   "));
            for (std::size_t i = 0; i < cur->kids.size(); ++i)
                render(cur->kids[i], childPrefix, i + 1 == cur->kids.size(), false);
        }

        // Print whole numbers as "3" rather than "3.000000".
        static std::string trim(double v) {
            std::string s = std::to_string(v);
            if (s.find('.') == std::string::npos)
                return s;
            s.erase(s.find_last_not_of('0') + 1);
            if (s.back() == '.')
                s.pop_back();
            return s;
        }

        Row m_row; // scratch: the Row the most recent visit() produced
    };
}
