//
// Created by bobi on 6. 9. 26.
//
//  Name resolution over the built AST -- the pass between AstBuilder and any
//  later type checking. It answers one question per name: has something with
//  this name been declared, in an enclosing scope, *before* this point?
//
//  What it checks:
//    - every identifier used as a value names a variable already in scope
//    - every call names a function already in scope
//    - every named type names a declared record
//    - no name is declared twice in the same scope, and no field twice in a record
//
//  What it does NOT check (that is type checking, not name resolution):
//    - whether a member access `p.x` names a real field of p's record
//    - call arity and argument types, or assignment targets being lvalues

#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "LangAst.h"

namespace Basic {
    class Resolver : public TypeVisitor,
                     public ExprVisitor,
                     public StmtVisitor,
                     public DeclVisitor {
    public:
        // Returns true when the program resolved cleanly. Resolution always runs
        // to completion, so errors() holds every problem found, not just the first.
        bool resolve(const Program &program);

        const std::vector<std::string> &errors() const { return m_errors; }

        // Print mode: emit every scope change, declaration and lookup as it
        // happens, indented by scope depth. Off by default; toggle before
        // resolve(), or mid-walk if you only want part of a program printed.
        void setPrint(bool on) { m_print = on; }
        bool printing() const { return m_print; }

        // ---- results ----
        // What each use of a name resolved to, for the phases that need to find
        // the declaration again. A variable is a VarDecl or a function parameter:
        // exactly one of the two is set. Null when the use did not resolve.
        struct VarRef {
            const VarDecl *var = nullptr;
            const Param *param = nullptr;
        };
        const VarRef *declOf(const NamedExpr *use) const;
        const FunDecl *declOf(const CallExpr *call) const;

        // ---- Type ----
        void visit(AtomicType &t) override;
        void visit(NamedType &t) override;
        void visit(ArrayType &t) override;
        void visit(RefType &t) override;

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
        void visit(RefExpr &e) override;

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
        // What a name was declared as. Kept so a name can be reported as being
        // the wrong kind of thing ("'square' is a function, not a variable")
        // rather than merely undeclared.
        enum class Kind { Var, Fun, Record };
        static const char *kindName(Kind kind);

        // A declared name: its kind, plus the declaration itself for the kinds a
        // later phase needs to find again (at most one of var / param / fun).
        struct Entry {
            Kind kind;
            const VarDecl *var = nullptr;
            const Param *param = nullptr;
            const FunDecl *fun = nullptr;
        };

        using Scope = std::unordered_map<std::string, Entry>;

        void declare(const std::string &name, Entry entry);
        // Innermost scope outwards. On a hit, *depth (when given) receives the
        // 0-based index of the scope the name was found in -- 0 is global.
        const Entry *lookup(const std::string &name, std::size_t *depth = nullptr) const;
        // The entry when `name` is a `expected`; null, after reporting why, otherwise.
        const Entry *use(const std::string &name, Kind expected, const char *what);
        void error(std::string message) { m_errors.push_back(std::move(message)); }

        void pushScope(const char *what);
        void popScope();

        // Writes one resolution event to stdout, indented by current scope depth.
        // No-op unless print mode is on. Goes to std::cout rather than
        // Utils::Logger on purpose: the logger prefixes every line with a
        // timestamp and source location, which would bury the indentation this
        // relies on -- same reason AstPrinter and printTree use std::cout.
        void print(const std::string &message) const;

        // Null children (an absent initialiser, a bare return, a missing else)
        // are simply nothing to resolve.
        template <typename T>
        void walk(const std::unique_ptr<T> &node) {
            if (node)
                node->accept(*this);
        }

        std::vector<Scope> m_scopes; // back() is the innermost
        std::unordered_map<const NamedExpr *, VarRef> m_names;
        std::unordered_map<const CallExpr *, const FunDecl *> m_calls;
        std::vector<std::string> m_errors;
        bool m_print = false;
    };
}
