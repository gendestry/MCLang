//
// Created by bobi on 15. 9. 26.
//
//  Intermediate code generation -- the pass after Memory. Every function body
//  becomes one statement tree, and every name becomes an address:
//
//    - a global is MEM(NAME label), a local or parameter MEM(FP + offset),
//      reached through static links when it lives in an outer frame
//    - a `&T` parameter holds an address, so using it reads through once more
//    - p.x is MEM(address of p + field offset), a[i] MEM(address of a + i * size)
//    - a call writes the static link at offset 0 and the arguments after it
//    - `return` moves its value into RV and jumps to the function's exit label
//    - && and || only evaluate their right side when they have to
//
//  Every MEM carries the size of what it holds, so a record or array is
//  assigned and passed as one whole value.
//
//  Not supported yet, and reported as errors: an initialiser on a global, and
//  a function returning a value wider than one slot.

#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "LangAst.h"
#include "Mem.h"
#include "Resolver.h" // CMD_BUILTIN
#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/names/ImcLabel.h"
#include "ImcGen/data/stmt/ImcCMD.h"
#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    class Memory;
    class Resolver;

    class ImcGen : public ExprVisitor,
                   public StmtVisitor,
                   public DeclVisitor {
    public:
        ImcGen(const Resolver &resolver, const Memory &memory)
            : m_resolver(resolver), m_memory(memory) {}

        // Returns true when every function was generated. Like the other passes,
        // the walk runs to completion, so errors() holds every problem found.
        bool compute(const Program &program);

        const std::vector<std::string> &errors() const { return m_errors; }

        // Print mode: dump each function's tree once it is generated.
        void setPrint(bool on) { m_print = on; }
        bool printing() const { return m_print; }

        // ---- results ----
        struct Function {
            const FunDecl *decl;
            const MemFrame *frame;
            ImcLabel entry; // where the body starts, after the prologue
            ImcLabel exit;  // where the epilogue starts; every return jumps here
            ImcStmtPtr body;
        };

        struct String {
            ImcLabel label;
            std::string value;
        };

        // Non-const so the next phase can take the bodies over.
        std::vector<Function> &functions() { return m_functions; }
        const std::vector<String> &strings() const { return m_strings; }

        // ---- Expr ----
        void visit(NumberExpr &e) override;
        void visit(BoolExpr &e) override;
        void visit(StringExpr &e) override;
        void visit(BinaryExpr &e) override;
        void visit(UnaryExpr &e) override;
        void visit(CallExpr &e) override;
        void visit(NamedExpr &e) override;
        void visit(AccessExpr &e) override;
        void visit(IndexExpr &e) override;
        void visit(AddressExpr &e) override;
        void visit(DerefExpr &e) override;

        // ---- Stmt ----
        void visit(CompoundStmt &s) override;
        void visit(VarDeclStmt &s) override;
        void visit(ExprStmt &s) override;
        void visit(ReturnStmt &s) override;
        void visit(AssignStmt &s) override;
        void visit(IfStmt &s) override;
        void visit(WhileStmt &s) override;
        void visit(ForStmt &s) override;

        // ---- Decl ----
        void visit(VarDecl &d) override;
        void visit(FunDecl &d) override;
        void visit(RecordDecl &d) override;

    private:
        // Code for an expression. Afterwards m_type holds its type, as written in
        // the declaration it came from (null for a plain one-slot value).
        ImcExprPtr gen(const ExprPtr &e);
        ImcStmtPtr gen(const StmtPtr &s); // a missing statement is an empty STMTS

        ImcExprPtr shortCircuit(BinaryExpr &e);
        // cmd("...", ...) -> the CMD statement that emits it, keeping its success or
        // result in `dst` when there is one.
        ImcStmtPtr genCommand(CallExpr &call, ImcCMD::Store store, std::optional<ImcTemp> dst);

        // FP of the frame at static depth `depth`, reached from the current frame
        // through the static links.
        ImcExprPtr frameBase(std::size_t depth) const;
        ImcExprPtr addressOf(const MemAccess &access) const;
        // MEM(a) -> a. Only called on lvalues, which always lower to a MEM.
        static ImcExprPtr addressOf(ImcExprPtr value);

        std::size_t sizeOf(const Type *type) const; // null -> one slot
        const Type *fieldType(const std::string &record, const std::string &field) const;

        // Stands in for an expression that could not be generated; it is a MEM so
        // an enclosing lvalue can still take its address.
        ImcExprPtr failed(const std::string &message);

        void error(std::string message) { m_errors.push_back(std::move(message)); }
        void print(const std::string &message) const;

        const Resolver &m_resolver;
        const Memory &m_memory;
        std::unordered_map<std::string, const RecordDecl *> m_records;

        std::vector<Function> m_functions;
        std::vector<String> m_strings;

        // ---- scratch: what the last visit produced ----
        ImcExprPtr m_expr;
        const Type *m_type = nullptr;
        ImcStmtPtr m_stmt;

        // ---- state while a function body is being generated ----
        const MemFrame *m_frame = nullptr; // null at global level
        const ImcLabel *m_exit = nullptr;

        std::vector<std::string> m_errors;
        bool m_print = false;
    };
}
