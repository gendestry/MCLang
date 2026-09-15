//
// Created by bobi on 6. 9. 26.
//
//  Type checking over the built AST -- the pass after Resolver. Names are
//  already known to resolve by the time this runs, so this pass only asks what
//  each expression's type is and whether the surrounding context accepts it.
//
//  What it checks:
//    - operands of arithmetic, comparison and logical operators
//    - conditions of if / while / for are bool
//    - initialisers and assignments match the declared type, and assignment
//      targets are lvalues
//    - calls: the callee's arity and each argument's type
//    - `return` carries a value of the function's return type (and nothing in a
//      void function)
//    - member access names a real field of the base's record
//    - nothing is declared with type void
//
//  Errors are collected, not thrown: an expression that fails to type becomes
//  the Error type, which is compatible with everything, so one mistake does not
//  cascade into a page of follow-on complaints.

#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "LangAst.h"

namespace Basic {
    class TypeResolver : public TypeVisitor,
                         public ExprVisitor,
                         public StmtVisitor,
                         public DeclVisitor {
    public:
        // Returns true when the program type checked cleanly. Like Resolver, the
        // walk always runs to completion, so errors() holds every problem found.
        bool resolve(const Program &program);

        const std::vector<std::string> &errors() const { return m_errors; }

        // Print mode: emit the type of every expression and declaration as it is
        // computed, indented by scope depth. Off by default.
        void setPrint(bool on) { m_print = on; }
        bool printing() const { return m_print; }

        // ---- Type ----
        void visit(AtomicType &t) override;
        void visit(NamedType &t) override;

        // ---- Expr ----
        void visit(NumberExpr &e) override;
        void visit(BoolExpr &e) override;
        void visit(StringExpr &e) override;
        void visit(BinaryExpr &e) override;
        void visit(UnaryExpr &e) override;
        void visit(CallExpr &e) override;
        void visit(NamedExpr &e) override;
        void visit(AccessExpr &e) override;

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
        // A type is one of the four built-ins or a record, named by the record's
        // own name. Error is the "already complained about this" type: it matches
        // anything, so it never produces a second error further up the tree.
        struct Ty {
            enum class Kind { Float, Bool, String, Void, Record, Error } kind = Kind::Error;
            std::string record; // only meaningful when kind == Record

            bool is(Kind k) const { return kind == k; }
            bool isError() const { return kind == Kind::Error; }
            std::string name() const;
            // Error absorbs: comparing against it always succeeds.
            bool accepts(const Ty &other) const;
        };

        static Ty makeFloat() { return {Ty::Kind::Float, {}}; }
        static Ty makeBool() { return {Ty::Kind::Bool, {}}; }
        static Ty makeString() { return {Ty::Kind::String, {}}; }
        static Ty makeVoid() { return {Ty::Kind::Void, {}}; }
        static Ty makeError() { return {Ty::Kind::Error, {}}; }
        static Ty makeRecord(std::string name) { return {Ty::Kind::Record, std::move(name)}; }

        struct FunSig {
            Ty ret;
            std::vector<Ty> params;
        };

        // Fields in declaration order; records are small, so a linear find is
        // cheaper than a map and keeps the order for error messages.
        using Fields = std::vector<std::pair<std::string, Ty>>;

        // ---- gathering ----
        // Records and functions are collected before the bodies are walked, so a
        // call may name a function declared later in the file. (Resolver, which
        // enforces declare-before-use, has already had its say on ordering.)
        void collectSignatures(const Program &program);

        // ---- expression typing ----
        Ty typeOf(const ExprPtr &e); // null expr -> Void
        // Types a condition, traces it, and reports it if it is not a bool.
        void checkCondition(const ExprPtr &cond, const char *what);
        Ty typeOfType(const TypePtr &t); // a written type annotation -> Ty
        static bool isLValue(const Expr &e);

        // ---- environment ----
        void pushScope(const char *what);
        void popScope();
        void declare(const std::string &name, const Ty &type);
        const Ty *lookupVar(const std::string &name) const;
        const Fields *lookupRecord(const std::string &name) const;

        void error(std::string message) { m_errors.push_back(std::move(message)); }
        void print(const std::string &message) const;

        template <typename T>
        void walk(const std::unique_ptr<T> &node) {
            if (node)
                node->accept(*this);
        }

        std::vector<std::unordered_map<std::string, Ty>> m_scopes; // back() is innermost
        std::unordered_map<std::string, Fields> m_records;
        std::unordered_map<std::string, FunSig> m_functions;

        Ty m_result; // the type the last visited expression produced
        Ty m_returnType; // return type of the function being walked
        std::string m_function; // its name, for error messages

        std::vector<std::string> m_errors;
        bool m_print = false;
    };
}
