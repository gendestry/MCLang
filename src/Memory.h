//
// Created by bobi on 6. 9. 26.
//
//  Memory layout: frames and accesses. The pass after TypeResolver and the last
//  one that looks at the AST as a source-level thing -- it answers, for every
//  declaration, *where does this live*, so that lowering can stop dealing in
//  names and start dealing in addresses.
//
//  What it produces:
//    - a MemLayout per record: total size and the offset of each field
//    - a MemAccess per variable: an absolute label for a global, an offset from
//      the frame pointer for a parameter or a local
//    - a MemFrame per function: label, static depth, and the sizes of the two
//      blocks (locals, outgoing arguments) that only a full walk of the body can
//      reveal
//
//  No errors are reported here. By this point the program has been resolved and
//  type checked, so every name and every field is known to exist.

#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "LangAst.h"
#include "Mem.h"

namespace Basic {
    class Memory : public TypeVisitor,
                   public ExprVisitor,
                   public StmtVisitor,
                   public DeclVisitor {
    public:
        void compute(const Program &program);

        // Print mode: emit each record layout, frame and access as it is decided.
        void setPrint(bool on) { m_print = on; }
        bool printing() const { return m_print; }

        // ---- results ----
        // Null when the declaration was never seen (which should not happen for a
        // program that got this far).
        const MemAccess *accessOf(const VarDecl *decl) const;
        const MemAccess *accessOf(const Param *param) const;
        const MemFrame *frameOf(const FunDecl *decl) const;
        const MemLayout *layoutOf(const std::string &record) const;

        // ---- Type ----
        void visit(AtomicType &t) override;
        void visit(NamedType &t) override;
        void visit(ArrayType &t) override;

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
        // Records are laid out before anything else, so a field or a variable of
        // a record type already knows its size when it is placed.
        void layoutRecords(const Program &program);
        void measureCalls(const Program &program);

        std::size_t sizeOf(const TypePtr &type) const;

        void record(const VarDecl *decl, std::unique_ptr<MemAccess> access);
        void print(const std::string &message) const;

        template <typename T>
        void walk(const std::unique_ptr<T> &node) {
            if (node)
                node->accept(*this);
        }

        std::unordered_map<std::string, MemLayout> m_layouts;
        std::unordered_map<const VarDecl *, std::unique_ptr<MemAccess>> m_accesses;
        std::unordered_map<const Param *, std::unique_ptr<MemAccess>> m_paramAccesses;
        std::unordered_map<const FunDecl *, MemFrame> m_frames;
        std::unordered_map<std::string, std::size_t> m_callCost; // callee -> args block

        // ---- state while a function body is being walked ----
        MemFrame *m_frame = nullptr; // null while at global level
        std::size_t m_depth = 0;     // 0 outside any function
        // Locals stack downwards from FP, so this is how far down we already are.
        std::size_t m_localsUsed = 0;

        // Every call in a body writes its arguments into the same block, so the
        // block is sized by the widest of them, not by their sum.
        std::size_t sizeOfCall(const CallExpr &call) const;

        bool m_print = false;
    };
}
