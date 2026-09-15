//
// Created by bobi on 6. 9. 26.
//
//  The walk has two jobs that happen at once. Declarations get an access as they
//  are met -- globals a label, locals a slot below FP -- and calls are measured
//  on the way past, because the outgoing-argument block of a frame is as wide as
//  the widest call in its body.
//
//  Frame layout, from high addresses down:
//
//      incoming args (static link at FP+0, first parameter at FP+8)
//      -------- FP --------
//      saved old FP, saved return address        (MemFrame::LINKAGE)
//      locals, growing downwards                 (MemFrame::locals)
//      outgoing args, written by every call      (MemFrame::args)
//      -------- SP --------

#include "Memory.h"

#include <iostream>

#include "Utils/Colors/Font.h"

namespace Basic {
    namespace {
        std::string nameText(const std::string &name) {
            return Utils::Font::colorYellow + "'" + name + "'" + Utils::Font::colorReset;
        }
        std::string placeText(const std::string &text) {
            return Utils::Font::colorMagenta + text + Utils::Font::colorReset;
        }
        std::string dimText(const std::string &text) {
            return Utils::Font::colorDim + text + Utils::Font::colorReset;
        }
    }

    void Memory::print(const std::string &message) const {
        if (!m_print)
            return;
        std::cout << std::string((m_depth + 1) * 2, ' ') << message << std::endl;
    }

    // ---- results -------------------------------------------------------------

    const MemAccess *Memory::accessOf(const VarDecl *decl) const {
        auto it = m_accesses.find(decl);
        return it == m_accesses.end() ? nullptr : it->second.get();
    }

    const MemAccess *Memory::accessOf(const Param *param) const {
        auto it = m_paramAccesses.find(param);
        return it == m_paramAccesses.end() ? nullptr : it->second.get();
    }

    const MemFrame *Memory::frameOf(const FunDecl *decl) const {
        auto it = m_frames.find(decl);
        return it == m_frames.end() ? nullptr : &it->second;
    }

    const MemLayout *Memory::layoutOf(const std::string &record) const {
        auto it = m_layouts.find(record);
        return it == m_layouts.end() ? nullptr : &it->second;
    }

    void Memory::record(const VarDecl *decl, std::unique_ptr<MemAccess> access) {
        print(dimText("var ") + nameText(decl->name) + dimText(" -> ")
              + placeText(access->toString()));
        m_accesses[decl] = std::move(access);
    }

    // ---- sizes ---------------------------------------------------------------

    std::size_t Memory::sizeOf(const TypePtr &type) const {
        if (const auto *named = dynamic_cast<const NamedType *>(type.get())) {
            const MemLayout *layout = layoutOf(named->name);
            return layout ? layout->size : SLOT_SIZE;
        }
        return SLOT_SIZE; // float and bool are one slot; void never reaches here
    }

    // Records are laid out in declaration order, which is enough: Resolver has
    // already rejected a record that mentions one declared later.
    void Memory::layoutRecords(const Program &program) {
        for (const DeclPtr &d : program) {
            const auto *rec = dynamic_cast<const RecordDecl *>(d.get());
            if (!rec)
                continue;

            MemLayout layout;
            for (const DeclPtr &f : rec->fields) {
                const auto *field = dynamic_cast<const VarDecl *>(f.get());
                if (!field)
                    continue;
                const std::size_t size = sizeOf(field->type);
                layout.fields.push_back({field->name, layout.size, size});
                layout.size += size;
            }
            m_layouts[rec->name] = std::move(layout);
        }
    }

    // What every call to a given function costs its caller's argument block. Done
    // up front so a call may name a function declared further down the file.
    void Memory::measureCalls(const Program &program) {
        for (const DeclPtr &d : program) {
            const auto *fun = dynamic_cast<const FunDecl *>(d.get());
            if (!fun)
                continue;
            std::size_t size = SLOT_SIZE; // the static link always goes first
            for (const Param &p : fun->params)
                size += sizeOf(p.type);
            m_callCost[fun->name] = size;
        }
    }

    // ---- driver --------------------------------------------------------------

    void Memory::compute(const Program &program) {
        m_layouts.clear();
        m_accesses.clear();
        m_frames.clear();
        m_paramAccesses.clear();
        m_callCost.clear();
        m_frame = nullptr;
        m_depth = 0;
        m_localsUsed = 0;

        layoutRecords(program);
        measureCalls(program);
        for (const DeclPtr &d : program)
            walk(d);
    }

    // ---- Type / Expr ---------------------------------------------------------
    //
    // Types carry no storage of their own, and most expressions are walked only
    // to reach the calls buried inside them.

    void Memory::visit(AtomicType &) {}
    void Memory::visit(NamedType &) {}

    void Memory::visit(NumberExpr &) {}
    void Memory::visit(BoolExpr &) {}
    void Memory::visit(StringExpr &) {}
    void Memory::visit(NamedExpr &) {}

    void Memory::visit(BinaryExpr &e) {
        walk(e.lhs);
        walk(e.rhs);
    }

    void Memory::visit(UnaryExpr &e) { walk(e.operand); }
    void Memory::visit(AccessExpr &e) { walk(e.base); }

    // The block a call needs is the static link plus its arguments -- sized from
    // the callee's parameter types, which are known exactly, rather than from the
    // argument expressions (whose types this phase does not carry).
    std::size_t Memory::sizeOfCall(const CallExpr &call) const {
        auto it = m_callCost.find(call.callee);
        return it == m_callCost.end() ? SLOT_SIZE : it->second;
    }

    void Memory::visit(CallExpr &e) {
        for (const ExprPtr &a : e.args)
            walk(a);
        if (!m_frame)
            return; // a call outside a function body has nowhere to put its args
        const std::size_t needed = sizeOfCall(e);
        if (needed > m_frame->args) {
            m_frame->args = needed;
            print(dimText("call ") + nameText(e.callee) + dimText(" -> args block ")
                  + placeText(std::to_string(needed) + "B"));
        }
    }

    // ---- Stmt ----------------------------------------------------------------
    //
    // Locals in sibling blocks each get their own slot rather than sharing one:
    // reusing slots is a size optimisation, and it would make a frame's layout
    // depend on control flow.

    void Memory::visit(CompoundStmt &s) {
        for (const StmtPtr &b : s.body)
            walk(b);
    }

    void Memory::visit(VarDeclStmt &s) { walk(s.decl); }
    void Memory::visit(ExprStmt &s) { walk(s.expr); }
    void Memory::visit(ReturnStmt &s) { walk(s.expr); }

    void Memory::visit(AssignStmt &s) {
        walk(s.target);
        walk(s.value);
    }

    void Memory::visit(IfStmt &s) {
        walk(s.cond);
        walk(s.then);
        walk(s.otherwise);
    }

    void Memory::visit(WhileStmt &s) {
        walk(s.cond);
        walk(s.body);
    }

    void Memory::visit(ForStmt &s) {
        walk(s.init);
        walk(s.cond);
        walk(s.step);
        walk(s.body);
    }

    // ---- Decl ----------------------------------------------------------------

    void Memory::visit(VarDecl &d) {
        const std::size_t size = sizeOf(d.type);
        walk(d.init); // an initialiser may contain calls

        if (!m_frame) {
            // A global: one instance, one address, named by a label.
            record(&d, std::make_unique<MemAbsAccess>(d.name, size));
            return;
        }

        // A local: below the linkage slots, growing downwards.
        m_localsUsed += size;
        const long long offset = -static_cast<long long>(MemFrame::LINKAGE + m_localsUsed);
        record(&d, std::make_unique<MemRelAccess>(offset, m_depth, size));
        m_frame->locals = m_localsUsed;
    }

    void Memory::visit(FunDecl &d) {
        MemFrame &frame = m_frames[&d];
        frame.label = d.name;
        frame.depth = m_depth + 1;

        // Printed before entering the frame, so it lines up with its siblings.
        print(dimText("function ") + nameText(d.name) + dimText(" depth ")
              + placeText(std::to_string(frame.depth)));

        MemFrame *outerFrame = m_frame;
        const std::size_t outerLocals = m_localsUsed;

        m_frame = &frame;
        m_depth = frame.depth;
        m_localsUsed = 0;

        // Parameters live above FP, in the block the caller wrote: the static link
        // sits at FP+0, so the first parameter starts one slot up.
        std::size_t offset = SLOT_SIZE;
        for (Param &p : d.params) {
            const std::size_t size = sizeOf(p.type);
            auto access = std::make_unique<MemRelAccess>(static_cast<long long>(offset),
                                                         frame.depth, size);
            print(dimText("param ") + nameText(p.name) + dimText(" -> ")
                  + placeText(access->toString()));
            m_paramAccesses[&p] = std::move(access);
            offset += size;
        }

        walk(d.body);

        print(dimText("frame ") + placeText(frame.toString()));

        m_frame = outerFrame;
        m_depth = frame.depth - 1;
        m_localsUsed = outerLocals;
    }

    // Fields are not variables: they have an offset inside their record, not an
    // address of their own, and layoutRecords has already assigned those.
    void Memory::visit(RecordDecl &d) {
        const MemLayout *layout = layoutOf(d.name);
        if (!layout)
            return;
        print(dimText("record ") + nameText(d.name) + dimText(" size ")
              + placeText(std::to_string(layout->size) + "B"));
        for (const MemLayout::Field &f : layout->fields)
            print(dimText("  field ") + nameText(f.name) + dimText(" @ ")
                  + placeText(std::to_string(f.offset) + " (" + std::to_string(f.size) + "B)"));
    }
}
