#pragma once
// AUTO-GENERATED from ast.spec by gen_ast.py — do not edit by hand.
#include <memory>
#include <string>
#include <vector>

namespace Basic
{
    // ---- forward declarations ----
    struct AtomicType;
    struct NamedType;
    struct ArrayType;
    struct RefType;
    struct NumberExpr;
    struct BoolExpr;
    struct StringExpr;
    struct BinaryExpr;
    struct CallExpr;
    struct NamedExpr;
    struct AccessExpr;
    struct IndexExpr;
    struct RefExpr;
    struct UnaryExpr;
    struct CompoundStmt;
    struct VarDeclStmt;
    struct ExprStmt;
    struct ReturnStmt;
    struct AssignStmt;
    struct IfStmt;
    struct WhileStmt;
    struct ForStmt;
    struct VarDecl;
    struct FunDecl;
    struct RecordDecl;

    // ---- visitor interfaces (one per category) ----
    struct TypeVisitor
    {
        virtual ~TypeVisitor() = default;
        virtual void visit(AtomicType &) = 0;
        virtual void visit(NamedType &) = 0;
        virtual void visit(ArrayType &) = 0;
        virtual void visit(RefType &) = 0;
    };

    struct ExprVisitor
    {
        virtual ~ExprVisitor() = default;
        virtual void visit(NumberExpr &) = 0;
        virtual void visit(BoolExpr &) = 0;
        virtual void visit(StringExpr &) = 0;
        virtual void visit(BinaryExpr &) = 0;
        virtual void visit(CallExpr &) = 0;
        virtual void visit(NamedExpr &) = 0;
        virtual void visit(AccessExpr &) = 0;
        virtual void visit(IndexExpr &) = 0;
        virtual void visit(RefExpr &) = 0;
        virtual void visit(UnaryExpr &) = 0;
    };

    struct StmtVisitor
    {
        virtual ~StmtVisitor() = default;
        virtual void visit(CompoundStmt &) = 0;
        virtual void visit(VarDeclStmt &) = 0;
        virtual void visit(ExprStmt &) = 0;
        virtual void visit(ReturnStmt &) = 0;
        virtual void visit(AssignStmt &) = 0;
        virtual void visit(IfStmt &) = 0;
        virtual void visit(WhileStmt &) = 0;
        virtual void visit(ForStmt &) = 0;
    };

    struct DeclVisitor
    {
        virtual ~DeclVisitor() = default;
        virtual void visit(VarDecl &) = 0;
        virtual void visit(FunDecl &) = 0;
        virtual void visit(RecordDecl &) = 0;
    };

    // ---- category bases + smart-pointer aliases ----
    struct Type
    {
        virtual ~Type() = default;
        virtual void accept(TypeVisitor &v) = 0;
    };
    using TypePtr = std::unique_ptr<Type>;

    struct Expr
    {
        virtual ~Expr() = default;
        virtual void accept(ExprVisitor &v) = 0;
    };
    using ExprPtr = std::unique_ptr<Expr>;

    struct Stmt
    {
        virtual ~Stmt() = default;
        virtual void accept(StmtVisitor &v) = 0;
    };
    using StmtPtr = std::unique_ptr<Stmt>;

    struct Decl
    {
        virtual ~Decl() = default;
        virtual void accept(DeclVisitor &v) = 0;
    };
    using DeclPtr = std::unique_ptr<Decl>;

    // ---- records (plain structs, not visited) ----
    struct Param
    {
        TypePtr type;
        std::string name;
    };

    // ---- concrete nodes ----
    struct AtomicType : Type
    {
        std::string prim;
        void accept(TypeVisitor &v) override { v.visit(*this); }
    };

    struct NamedType : Type
    {
        std::string name;
        void accept(TypeVisitor &v) override { v.visit(*this); }
    };

    struct ArrayType : Type
    {
        TypePtr elem;
        unsigned long long length = 0;
        void accept(TypeVisitor &v) override { v.visit(*this); }
    };

    struct RefType : Type
    {
        TypePtr elem;
        void accept(TypeVisitor &v) override { v.visit(*this); }
    };

    struct NumberExpr : Expr
    {
        double value = 0;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct BoolExpr : Expr
    {
        bool value = false;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct StringExpr : Expr
    {
        std::string value;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct BinaryExpr : Expr
    {
        std::string op;
        ExprPtr lhs;
        ExprPtr rhs;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct CallExpr : Expr
    {
        std::string callee;
        std::vector<ExprPtr> args;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct NamedExpr : Expr
    {
        std::string callee;
        std::vector<ExprPtr> args;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct AccessExpr : Expr
    {
        ExprPtr base;
        std::string member;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct IndexExpr : Expr
    {
        ExprPtr base;
        ExprPtr index;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct RefExpr : Expr
    {
        ExprPtr operand;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct UnaryExpr : Expr
    {
        std::string op;
        ExprPtr operand;
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct CompoundStmt : Stmt
    {
        std::vector<StmtPtr> body;
        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct VarDeclStmt : Stmt
    {
        DeclPtr decl;
        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct ExprStmt : Stmt
    {
        ExprPtr expr;
        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct ReturnStmt : Stmt
    {
        ExprPtr expr;
        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct AssignStmt : Stmt
    {
        ExprPtr target;
        ExprPtr value;
        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct IfStmt : Stmt
    {
        ExprPtr cond;
        StmtPtr then;
        StmtPtr otherwise;
        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct WhileStmt : Stmt
    {
        ExprPtr cond;
        StmtPtr body;
        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct ForStmt : Stmt
    {
        StmtPtr init;
        ExprPtr cond;
        StmtPtr step;
        StmtPtr body;
        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct VarDecl : Decl
    {
        TypePtr type;
        std::string name;
        ExprPtr init;
        void accept(DeclVisitor &v) override { v.visit(*this); }
    };

    struct FunDecl : Decl
    {
        TypePtr returnType;
        std::string name;
        std::vector<Param> params;
        StmtPtr body;
        void accept(DeclVisitor &v) override { v.visit(*this); }
    };

    struct RecordDecl : Decl
    {
        std::string name;
        std::vector<DeclPtr> fields;
        void accept(DeclVisitor &v) override { v.visit(*this); }
    };

    using Program = std::vector<DeclPtr>;
}
