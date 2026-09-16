//
// Created by bobi on 15. 9. 26.
//

#include "ImcGen/ImcGen.h"

#include <iostream>
#include <stdexcept>

#include "ImcGen/ImcPrinter.h"
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
#include "Memory.h"
#include "Resolver.h"
#include "Utils/Colors/Font.h"

namespace Basic {
    namespace {
        std::string nameText(const std::string &name) {
            return Utils::Font::colorYellow + "'" + name + "'" + Utils::Font::colorReset;
        }
        std::string dimText(const std::string &text) {
            return Utils::Font::colorDim + text + Utils::Font::colorReset;
        }

        ImcExprPtr constant(double value) { return std::make_unique<ImcCONST>(value); }

        ImcExprPtr plus(ImcExprPtr lhs, ImcExprPtr rhs) {
            return std::make_unique<ImcBINOP>(ImcBINOP::Oper::ADD, std::move(lhs), std::move(rhs));
        }

        bool binaryOper(const std::string &op, ImcBINOP::Oper &out) {
            static const std::unordered_map<std::string, ImcBINOP::Oper> opers = {
                {"+", ImcBINOP::Oper::ADD},  {"-", ImcBINOP::Oper::SUB},  {"*", ImcBINOP::Oper::MUL},
                {"/", ImcBINOP::Oper::DIV},  {"==", ImcBINOP::Oper::EQU}, {"!=", ImcBINOP::Oper::NEQ},
                {"<", ImcBINOP::Oper::LTH},  {">", ImcBINOP::Oper::GTH},  {"<=", ImcBINOP::Oper::LEQ},
                {">=", ImcBINOP::Oper::GEQ},
            };
            auto it = opers.find(op);
            if (it == opers.end())
                return false;
            out = it->second;
            return true;
        }
    }

    void ImcGen::print(const std::string &message) const {
        if (!m_print)
            return;
        std::cout << "  " << message << std::endl;
    }

    // ---- driver --------------------------------------------------------------

    bool ImcGen::compute(const Program &program) {
        m_errors.clear();
        m_functions.clear();
        m_strings.clear();
        m_records.clear();
        m_frame = nullptr;
        m_exit = nullptr;

        for (const DeclPtr &d : program)
            if (const auto *rec = dynamic_cast<const RecordDecl *>(d.get()))
                m_records[rec->name] = rec;

        for (const DeclPtr &d : program)
            d->accept(*this);
        return m_errors.empty();
    }

    ImcExprPtr ImcGen::gen(const ExprPtr &e) {
        m_type = nullptr;
        if (!e)
            return constant(0);
        e->accept(*this);
        return std::move(m_expr);
    }

    ImcStmtPtr ImcGen::gen(const StmtPtr &s) {
        if (!s)
            return std::make_unique<ImcSTMTS>();
        s->accept(*this);
        return std::move(m_stmt);
    }

    ImcExprPtr ImcGen::failed(const std::string &message) {
        error(message);
        m_type = nullptr;
        return std::make_unique<ImcMEM>(constant(0));
    }

    // ---- addresses and sizes -------------------------------------------------

    ImcExprPtr ImcGen::frameBase(std::size_t depth) const {
        ImcExprPtr base = std::make_unique<ImcTEMP>(ImcTemp::FP());
        // Each hop outwards reads the static link, which every frame keeps at FP+0.
        for (std::size_t d = m_frame ? m_frame->depth : 0; d > depth; --d)
            base = std::make_unique<ImcMEM>(std::move(base));
        return base;
    }

    ImcExprPtr ImcGen::addressOf(const MemAccess &access) const {
        if (const auto *abs = dynamic_cast<const MemAbsAccess *>(&access))
            return std::make_unique<ImcNAME>(ImcLabel(abs->label));
        const auto &rel = static_cast<const MemRelAccess &>(access);
        return plus(frameBase(rel.depth), constant(static_cast<double>(rel.offset)));
    }

    ImcExprPtr ImcGen::addressOf(ImcExprPtr value) {
        auto *mem = dynamic_cast<ImcMEM *>(value.get());
        if (!mem)
            throw std::logic_error("ImcGen: taking the address of a value that is not in memory: "
                                   + value->toString());
        return std::move(mem->addr);
    }

    std::size_t ImcGen::sizeOf(const Type *type) const {
        if (const auto *named = dynamic_cast<const NamedType *>(type)) {
            const MemLayout *layout = m_memory.layoutOf(named->name);
            return layout ? layout->size : SLOT_SIZE;
        }
        if (const auto *array = dynamic_cast<const ArrayType *>(type))
            return array->length * sizeOf(array->elem.get());
        return SLOT_SIZE; // float, bool, and a pointer (an address)
    }

    const Type *ImcGen::fieldType(const std::string &record, const std::string &field) const {
        auto it = m_records.find(record);
        if (it == m_records.end())
            return nullptr;
        for (const DeclPtr &f : it->second->fields)
            if (const auto *var = dynamic_cast<const VarDecl *>(f.get()); var && var->name == field)
                return var->type.get();
        return nullptr;
    }

    // ---- Expr ----------------------------------------------------------------

    void ImcGen::visit(NumberExpr &e) {
        m_expr = constant(e.value);
        m_type = nullptr;
    }

    void ImcGen::visit(BoolExpr &e) {
        m_expr = constant(e.value ? 1 : 0);
        m_type = nullptr;
    }

    // The characters live in the data segment; the expression is their address.
    void ImcGen::visit(StringExpr &e) {
        ImcLabel label("S" + std::to_string(m_strings.size()));
        m_strings.push_back({label, e.value});
        m_expr = std::make_unique<ImcNAME>(std::move(label));
        m_type = nullptr;
    }

    void ImcGen::visit(BinaryExpr &e) {
        if (e.op == "&&" || e.op == "||") {
            m_expr = shortCircuit(e);
            m_type = nullptr;
            return;
        }

        ImcBINOP::Oper oper;
        if (!binaryOper(e.op, oper)) {
            m_expr = failed("ImcGen: unknown binary operator '" + e.op + "'");
            return;
        }
        ImcExprPtr lhs = gen(e.lhs);
        ImcExprPtr rhs = gen(e.rhs);
        m_expr = std::make_unique<ImcBINOP>(oper, std::move(lhs), std::move(rhs));
        m_type = nullptr;
    }

    //  result <- lhs
    //  CJUMP result, (&&: evalRhs, end | ||: end, evalRhs)
    //  evalRhs: result <- rhs
    //  end:     ... the value is result
    ImcExprPtr ImcGen::shortCircuit(BinaryExpr &e) {
        const bool isAnd = e.op == "&&";
        const ImcTemp result = ImcTemp::fresh();
        const ImcLabel evalRhs = ImcLabel::fresh();
        const ImcLabel end = ImcLabel::fresh();

        auto stmts = std::make_unique<ImcSTMTS>();
        stmts->add(std::make_unique<ImcMOVE>(std::make_unique<ImcTEMP>(result), gen(e.lhs)));
        // && only needs its right side when the left was true, || when it was false.
        stmts->add(std::make_unique<ImcCJUMP>(std::make_unique<ImcTEMP>(result), isAnd ? evalRhs : end,
                                              isAnd ? end : evalRhs));
        stmts->add(std::make_unique<ImcLABEL>(evalRhs));
        stmts->add(std::make_unique<ImcMOVE>(std::make_unique<ImcTEMP>(result), gen(e.rhs)));
        stmts->add(std::make_unique<ImcLABEL>(end));
        return std::make_unique<ImcSEXPR>(std::move(stmts), std::make_unique<ImcTEMP>(result));
    }

    void ImcGen::visit(UnaryExpr &e) {
        ImcExprPtr operand = gen(e.operand);
        const auto oper = e.op == "-" ? ImcUNOP::Oper::NEG : ImcUNOP::Oper::NOT;
        m_expr = std::make_unique<ImcUNOP>(oper, std::move(operand));
        m_type = nullptr;
    }

    void ImcGen::visit(CallExpr &e) {
        // A command used as a value: run it, then read what it left behind.
        if (isCommandBuiltin(e.callee)) {
            const ImcTemp result = ImcTemp::fresh();
            ImcStmtPtr cmd = genCommand(e, e.callee == CMD_BUILTIN ? ImcCMD::Store::Success
                                                                    : ImcCMD::Store::Result, result);
            m_expr = std::make_unique<ImcSEXPR>(std::move(cmd), std::make_unique<ImcTEMP>(result));
            m_type = nullptr;
            return;
        }

        const FunDecl *fun = m_resolver.declOf(&e);
        const MemFrame *frame = fun ? m_memory.frameOf(fun) : nullptr;
        if (!frame) {
            m_expr = failed("ImcGen: call to '" + e.callee + "' has no frame");
            return;
        }

        auto call = std::make_unique<ImcCALL>(ImcLabel(frame->label));
        // The static link is the FP of the frame the callee was declared in.
        call->addArg(0, SLOT_SIZE, frameBase(frame->depth - 1));

        std::size_t offset = SLOT_SIZE;
        for (const ExprPtr &a : e.args) {
            ImcExprPtr arg = gen(a);
            const std::size_t size = sizeOf(m_type); // a `&x` argument has no type: one slot
            call->addArg(offset, size, std::move(arg));
            offset += size;
        }

        m_expr = std::move(call);
        m_type = fun->returnType.get();
    }

    void ImcGen::visit(NamedExpr &e) {
        const Resolver::VarRef *ref = m_resolver.declOf(&e);
        const MemAccess *access = !ref ? nullptr
                                  : ref->var ? m_memory.accessOf(ref->var)
                                             : m_memory.accessOf(ref->param);
        if (!access) {
            m_expr = failed("ImcGen: variable '" + e.callee + "' has no storage");
            return;
        }

        ImcExprPtr addr = addressOf(*access);
        const Type *type = ref->var ? ref->var->type.get() : ref->param->type.get();

        m_expr = std::make_unique<ImcMEM>(std::move(addr), sizeOf(type));
        m_type = type;
    }

    void ImcGen::visit(AccessExpr &e) {
        ImcExprPtr base = gen(e.base);
        const auto *record = dynamic_cast<const NamedType *>(m_type);
        const MemLayout *layout = record ? m_memory.layoutOf(record->name) : nullptr;
        const MemLayout::Field *field = layout ? layout->find(e.member) : nullptr;
        const Type *type = record ? fieldType(record->name, e.member) : nullptr;
        if (!field || !type) {
            m_expr = failed("ImcGen: no field '" + e.member + "' to access");
            return;
        }

        ImcExprPtr addr = plus(addressOf(std::move(base)), constant(static_cast<double>(field->offset)));
        m_expr = std::make_unique<ImcMEM>(std::move(addr), field->size);
        m_type = type;
    }

    // a[i] is MEM(address of a + i * size); p[i] is MEM(p + i * size) -- the
    // pointer's value already is the address the elements start at.
    void ImcGen::visit(IndexExpr &e) {
        ImcExprPtr base = gen(e.base);
        const auto *array = dynamic_cast<const ArrayType *>(m_type);
        const auto *pointer = dynamic_cast<const PointerType *>(m_type);
        ImcExprPtr index = gen(e.index);
        if (!array && !pointer) {
            m_expr = failed("ImcGen: indexing something that is neither an array nor a pointer");
            return;
        }

        const Type *elem = array ? array->elem.get() : pointer->elem.get();
        const std::size_t size = sizeOf(elem);
        ImcExprPtr start = array ? addressOf(std::move(base)) : std::move(base);
        ImcExprPtr offset = std::make_unique<ImcBINOP>(ImcBINOP::Oper::MUL, std::move(index),
                                                       constant(static_cast<double>(size)));
        m_expr = std::make_unique<ImcMEM>(plus(std::move(start), std::move(offset)), size);
        m_type = elem;
    }

    // Where the operand lives, not what it holds. The result has no written type
    // to point back to, so indexing or dereferencing it goes through a variable
    // first -- except *&x, which is just x.
    void ImcGen::visit(AddressExpr &e) {
        m_expr = addressOf(gen(e.operand));
        m_type = nullptr;
    }

    void ImcGen::visit(DerefExpr &e) {
        if (auto *address = dynamic_cast<AddressExpr *>(e.operand.get())) {
            m_expr = gen(address->operand);
            return;
        }
        ImcExprPtr pointer = gen(e.operand);
        const auto *type = dynamic_cast<const PointerType *>(m_type);
        if (!type) {
            m_expr = failed("ImcGen: dereferencing something that is not a pointer");
            return;
        }
        m_expr = std::make_unique<ImcMEM>(std::move(pointer), sizeOf(type->elem.get()));
        m_type = type->elem.get();
    }

    // ---- Stmt ----------------------------------------------------------------

    void ImcGen::visit(CompoundStmt &s) {
        auto stmts = std::make_unique<ImcSTMTS>();
        for (const StmtPtr &b : s.body)
            stmts->add(gen(b));
        m_stmt = std::move(stmts);
    }

    void ImcGen::visit(VarDeclStmt &s) {
        m_stmt = std::make_unique<ImcSTMTS>();
        if (s.decl)
            s.decl->accept(*this); // VarDecl leaves its initialisation in m_stmt
    }

    void ImcGen::visit(ExprStmt &s) {
        // A command whose value is dropped needn't store it anywhere.
        if (auto *call = dynamic_cast<CallExpr *>(s.expr.get()); call && isCommandBuiltin(call->callee)) {
            m_stmt = genCommand(*call, ImcCMD::Store::None, std::nullopt);
            return;
        }
        m_stmt = std::make_unique<ImcESTMT>(gen(s.expr));
    }

    // TypeResolver has already checked the shape; anything still wrong here means
    // the program failed to type check, so keep quiet and generate nothing.
    ImcStmtPtr ImcGen::genCommand(CallExpr &call, ImcCMD::Store store, std::optional<ImcTemp> dst) {
        const auto *text = call.args.empty() ? nullptr
                                             : dynamic_cast<const StringExpr *>(call.args[0].get());
        if (!text) {
            error("ImcGen: " + call.callee + "() without a literal command");
            return std::make_unique<ImcSTMTS>();
        }

        auto cmd = dst ? std::make_unique<ImcCMD>(text->value, store, *dst) : std::make_unique<ImcCMD>(text->value);
        for (std::size_t i = 1; i < call.args.size(); ++i)
            cmd->addArg(gen(call.args[i]));
        return cmd;
    }

    void ImcGen::visit(ReturnStmt &s) {
        auto stmts = std::make_unique<ImcSTMTS>();
        if (s.expr)
            stmts->add(std::make_unique<ImcMOVE>(std::make_unique<ImcTEMP>(ImcTemp::RV()), gen(s.expr)));
        stmts->add(std::make_unique<ImcJUMP>(*m_exit));
        m_stmt = std::move(stmts);
    }

    void ImcGen::visit(AssignStmt &s) {
        ImcExprPtr dst = gen(s.target);
        ImcExprPtr src = gen(s.value);
        m_stmt = std::make_unique<ImcMOVE>(std::move(dst), std::move(src));
    }

    //  CJUMP cond, yes, no
    //  yes: then; JUMP end
    //  no:  else
    //  end:
    void ImcGen::visit(IfStmt &s) {
        const ImcLabel yes = ImcLabel::fresh(), no = ImcLabel::fresh(), end = ImcLabel::fresh();
        auto stmts = std::make_unique<ImcSTMTS>();
        stmts->add(std::make_unique<ImcCJUMP>(gen(s.cond), yes, no));
        stmts->add(std::make_unique<ImcLABEL>(yes));
        stmts->add(gen(s.then));
        stmts->add(std::make_unique<ImcJUMP>(end));
        stmts->add(std::make_unique<ImcLABEL>(no));
        stmts->add(gen(s.otherwise));
        stmts->add(std::make_unique<ImcLABEL>(end));
        m_stmt = std::move(stmts);
    }

    //  top:  CJUMP cond, body, end
    //  body: ...; JUMP top
    //  end:
    void ImcGen::visit(WhileStmt &s) {
        const ImcLabel top = ImcLabel::fresh(), body = ImcLabel::fresh(), end = ImcLabel::fresh();
        auto stmts = std::make_unique<ImcSTMTS>();
        stmts->add(std::make_unique<ImcLABEL>(top));
        stmts->add(std::make_unique<ImcCJUMP>(gen(s.cond), body, end));
        stmts->add(std::make_unique<ImcLABEL>(body));
        stmts->add(gen(s.body));
        stmts->add(std::make_unique<ImcJUMP>(top));
        stmts->add(std::make_unique<ImcLABEL>(end));
        m_stmt = std::move(stmts);
    }

    // A while loop with the init in front and the step at the end of the body.
    void ImcGen::visit(ForStmt &s) {
        const ImcLabel top = ImcLabel::fresh(), body = ImcLabel::fresh(), end = ImcLabel::fresh();
        auto stmts = std::make_unique<ImcSTMTS>();
        stmts->add(gen(s.init));
        stmts->add(std::make_unique<ImcLABEL>(top));
        stmts->add(std::make_unique<ImcCJUMP>(s.cond ? gen(s.cond) : constant(1), body, end));
        stmts->add(std::make_unique<ImcLABEL>(body));
        stmts->add(gen(s.body));
        stmts->add(gen(s.step));
        stmts->add(std::make_unique<ImcJUMP>(top));
        stmts->add(std::make_unique<ImcLABEL>(end));
        m_stmt = std::move(stmts);
    }

    // ---- Decl ----------------------------------------------------------------

    // Storage was assigned by Memory; all a declaration can add is its initialiser.
    void ImcGen::visit(VarDecl &d) {
        m_stmt = std::make_unique<ImcSTMTS>();
        if (!d.init)
            return;
        if (!m_frame) {
            error("initialising the global '" + d.name + "' is not supported yet");
            return;
        }

        const MemAccess *access = m_memory.accessOf(&d);
        if (!access) {
            error("ImcGen: variable '" + d.name + "' has no storage");
            return;
        }
        auto dst = std::make_unique<ImcMEM>(addressOf(*access), sizeOf(d.type.get()));
        m_stmt = std::make_unique<ImcMOVE>(std::move(dst), gen(d.init));
    }

    void ImcGen::visit(FunDecl &d) {
        const MemFrame *frame = m_memory.frameOf(&d);
        if (!frame) {
            error("ImcGen: function '" + d.name + "' has no frame");
            return;
        }
        if (sizeOf(d.returnType.get()) > SLOT_SIZE) {
            error("function '" + d.name + "' returns a value wider than one slot, which is not supported yet");
            return;
        }

        ImcLabel entry = ImcLabel::fresh();
        ImcLabel exit = ImcLabel::fresh();

        const MemFrame *outerFrame = m_frame;
        const ImcLabel *outerExit = m_exit;
        m_frame = frame;
        m_exit = &exit;
        ImcStmtPtr body = gen(d.body);
        m_frame = outerFrame;
        m_exit = outerExit;

        if (m_print) {
            print(dimText("function ") + nameText(d.name) + dimText(" entry " + entry.name + ", exit " + exit.name));
            ImcPrinter(2).print(*body);
        }

        m_functions.push_back({&d, frame, std::move(entry), std::move(exit), std::move(body)});
    }

    void ImcGen::visit(RecordDecl &) {} // a record's layout is all it has, and Memory made that
}
