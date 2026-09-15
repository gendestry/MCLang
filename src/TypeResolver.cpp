//
// Created by bobi on 6. 9. 26.
//
//  The walk mirrors Resolver's: the same scope stack, the same "collect every
//  error" policy. The one addition is m_result, which each Expr visit sets to
//  the type it produced -- the visitors return void, so typeOf() reads the type
//  back out of that slot right after dispatching.

#include "TypeResolver.h"

#include <iostream>

#include "Utils/Colors/Font.h"

namespace Basic {
    namespace {
        std::string typeText(const std::string &name) {
            return Utils::Font::colorMagenta + name + Utils::Font::colorReset;
        }
        std::string nameText(const std::string &name) {
            return Utils::Font::colorYellow + "'" + name + "'" + Utils::Font::colorReset;
        }
        std::string badText(const std::string &text) {
            return Utils::Font::colorRed + text + Utils::Font::colorReset;
        }
        std::string dimText(const std::string &text) {
            return Utils::Font::colorDim + text + Utils::Font::colorReset;
        }
    }

    std::string TypeResolver::Ty::name() const {
        switch (kind) {
        case Kind::Float: return "float";
        case Kind::Bool: return "bool";
        case Kind::String: return "string";
        case Kind::Void: return "void";
        case Kind::Record: return record;
        case Kind::Array: {
            // Written the way it is declared: float[2][3], outermost length first.
            std::string lengths;
            const Ty *t = this;
            for (; t->is(Kind::Array); t = t->elem.get())
                lengths += "[" + std::to_string(t->length) + "]";
            return t->name() + lengths;
        }
        case Kind::Ref: return "&" + elem->name();
        case Kind::Error: return "<error>";
        }
        return "<error>";
    }

    bool TypeResolver::Ty::accepts(const Ty &other) const {
        if (isError() || other.isError())
            return true; // already reported; do not complain twice
        if (kind != other.kind)
            return false;
        if (kind == Kind::Array || kind == Kind::Ref) // a Ref's length is always 0
            return length == other.length && elem->accepts(*other.elem);
        return kind != Kind::Record || record == other.record;
    }

    void TypeResolver::print(const std::string &message) const {
        if (!m_print)
            return;
        std::cout << std::string(m_scopes.size() * 2, ' ') << message << std::endl;
    }

    void TypeResolver::pushScope(const char *what) {
        m_scopes.emplace_back();
        print(dimText("+ scope ") + dimText(what));
    }

    void TypeResolver::popScope() {
        print(dimText("- scope"));
        m_scopes.pop_back();
    }

    void TypeResolver::declare(const std::string &name, const Ty &type) {
        m_scopes.back()[name] = type;
        print(dimText("var ") + nameText(name) + dimText(" : ") + typeText(type.name()));
    }

    const TypeResolver::Ty *TypeResolver::lookupVar(const std::string &name) const {
        for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope) {
            auto it = scope->find(name);
            if (it != scope->end())
                return &it->second;
        }
        return nullptr;
    }

    const TypeResolver::Fields *TypeResolver::lookupRecord(const std::string &name) const {
        auto it = m_records.find(name);
        return it == m_records.end() ? nullptr : &it->second;
    }

    // ---- driver --------------------------------------------------------------

    bool TypeResolver::resolve(const Program &program) {
        m_errors.clear();
        m_scopes.clear();
        m_records.clear();
        m_functions.clear();

        pushScope("global");
        collectSignatures(program);
        for (const DeclPtr &d : program)
            walk(d);
        popScope();

        return m_errors.empty();
    }

    // Records first, so a function signature may mention any record, and a record
    // may hold a field of a record declared further down.
    void TypeResolver::collectSignatures(const Program &program) {
        for (const DeclPtr &d : program)
            if (auto *rec = dynamic_cast<RecordDecl *>(d.get()))
                m_records.emplace(rec->name, Fields{});

        for (const DeclPtr &d : program) {
            auto *rec = dynamic_cast<RecordDecl *>(d.get());
            if (!rec)
                continue;
            Fields fields;
            for (const DeclPtr &f : rec->fields)
                if (auto *field = dynamic_cast<VarDecl *>(f.get()))
                    fields.emplace_back(field->name, typeOfType(field->type));
            m_records[rec->name] = std::move(fields);
        }

        for (const DeclPtr &d : program) {
            auto *fun = dynamic_cast<FunDecl *>(d.get());
            if (!fun)
                continue;
            FunSig sig{typeOfType(fun->returnType), {}};
            for (const Param &p : fun->params)
                sig.params.push_back(typeOfType(p.type));
            m_functions[fun->name] = std::move(sig);
        }
    }

    TypeResolver::Ty TypeResolver::typeOfType(const TypePtr &t) {
        if (!t)
            return makeError();
        t->accept(*this);
        return m_result;
    }

    TypeResolver::Ty TypeResolver::typeOf(const ExprPtr &e) {
        if (!e)
            return makeVoid(); // a bare `return;` has nothing to type
        e->accept(*this);
        return m_result;
    }

    // Only a name, or a chain of field accesses and indexing off one, can be
    // assigned to.
    bool TypeResolver::isLValue(const Expr &e) {
        if (const auto *named = dynamic_cast<const NamedExpr *>(&e))
            return named->args.empty();
        if (const auto *access = dynamic_cast<const AccessExpr *>(&e))
            return access->base && isLValue(*access->base);
        if (const auto *index = dynamic_cast<const IndexExpr *>(&e))
            return index->base && isLValue(*index->base);
        return false;
    }

    // ---- Type ----------------------------------------------------------------

    void TypeResolver::visit(AtomicType &t) {
        if (t.prim == "float")
            m_result = makeFloat();
        else if (t.prim == "bool")
            m_result = makeBool();
        else if (t.prim == "void")
            m_result = makeVoid();
        else if (t.prim == "string")
            m_result = makeString();
        else {
            error("unknown primitive type '" + t.prim + "'");
            m_result = makeError();
        }
    }

    // Resolver has already rejected a name that is not a record, so an unknown
    // one here only means that error was reported; stay quiet and yield Error.
    void TypeResolver::visit(NamedType &t) {
        m_result = lookupRecord(t.name) ? makeRecord(t.name) : makeError();
    }

    void TypeResolver::visit(ArrayType &t) {
        const Ty elem = typeOfType(t.elem);
        if (elem.is(Ty::Kind::Void)) {
            error("an array cannot hold void");
            m_result = makeError();
            return;
        }
        m_result = elem.isError() ? makeError() : makeArray(elem, t.length);
    }

    void TypeResolver::visit(RefType &t) {
        const Ty elem = typeOfType(t.elem);
        if (elem.is(Ty::Kind::Void)) {
            error("cannot take void by reference");
            m_result = makeError();
            return;
        }
        m_result = elem.isError() ? makeError() : makeRef(elem);
    }

    // ---- Expr ----------------------------------------------------------------

    void TypeResolver::visit(NumberExpr &) { m_result = makeFloat(); }
    void TypeResolver::visit(BoolExpr &) { m_result = makeBool(); }
    void TypeResolver::visit(StringExpr &) { m_result = makeString(); }

    // Every condition site prints the same way, so the trace shows which construct
    // demanded the bool as well as what it actually got.
    void TypeResolver::checkCondition(const ExprPtr &cond, const char *what) {
        const Ty type = typeOf(cond);
        print(dimText(std::string(what) + " condition : ") + typeText(type.name()));
        if (!makeBool().accepts(type))
            error(std::string(what) + " condition must be bool, got " + type.name());
    }

    void TypeResolver::visit(BinaryExpr &e) {
        const Ty lhs = typeOf(e.lhs);
        const Ty rhs = typeOf(e.rhs);
        const std::string &op = e.op;

        const bool arithmetic = op == "+" || op == "-" || op == "*" || op == "/";
        const bool ordering = op == "<" || op == ">" || op == "<=" || op == ">=";
        const bool equality = op == "==" || op == "!=";
        const bool logical = op == "&&" || op == "||";

        auto require = [&](const Ty &want) {
            bool ok = true;
            for (const Ty *side : {&lhs, &rhs})
                if (!want.accepts(*side)) {
                    error("operator '" + op + "' expects " + want.name() + ", got "
                          + side->name());
                    ok = false;
                }
            return ok;
        };

        if (arithmetic) {
            m_result = require(makeFloat()) ? makeFloat() : makeError();
        } else if (ordering) {
            m_result = require(makeFloat()) ? makeBool() : makeError();
        } else if (logical) {
            m_result = require(makeBool()) ? makeBool() : makeError();
        } else if (equality) {
            // Any two values of the same type may be compared, records included.
            if (!lhs.accepts(rhs)) {
                error("cannot compare " + lhs.name() + " with " + rhs.name());
                m_result = makeError();
            } else {
                m_result = makeBool();
            }
        } else {
            error("unknown binary operator '" + op + "'");
            m_result = makeError();
        }

        print(dimText("binary ") + nameText(op) + dimText(" : ") + typeText(m_result.name()));
    }

    void TypeResolver::visit(UnaryExpr &e) {
        const Ty operand = typeOf(e.operand);
        const Ty want = e.op == "!" ? makeBool() : makeFloat();

        if (!want.accepts(operand)) {
            error("unary '" + e.op + "' expects " + want.name() + ", got " + operand.name());
            m_result = makeError();
        } else {
            m_result = operand.isError() ? want : operand;
        }
    }

    void TypeResolver::visit(CallExpr &e) {
        std::vector<Ty> args;
        args.reserve(e.args.size());
        for (const ExprPtr &a : e.args)
            args.push_back(typeOf(a));

        auto it = m_functions.find(e.callee);
        if (it == m_functions.end()) {
            m_result = makeError(); // Resolver already reported the unknown name
            return;
        }

        const FunSig &sig = it->second;
        if (args.size() != sig.params.size()) {
            error("function '" + e.callee + "' takes " + std::to_string(sig.params.size())
                  + " argument(s), but " + std::to_string(args.size()) + " were given");
        } else {
            for (std::size_t i = 0; i < args.size(); ++i)
                if (!sig.params[i].accepts(args[i]))
                    error("argument " + std::to_string(i + 1) + " of '" + e.callee
                          + "' expects " + sig.params[i].name() + ", got " + args[i].name());
        }

        m_result = sig.ret;
        print(dimText("call ") + nameText(e.callee) + dimText(" : ") + typeText(m_result.name()));
    }

    void TypeResolver::visit(NamedExpr &e) {
        const Ty *type = lookupVar(e.callee);
        m_result = type ? *type : makeError(); // unknown name: Resolver's problem
        print(dimText("name ") + nameText(e.callee) + dimText(" : ") + typeText(m_result.name()));
    }

    void TypeResolver::visit(AccessExpr &e) {
        const Ty base = typeOf(e.base);
        if (base.isError()) {
            m_result = makeError();
            return;
        }
        if (!base.is(Ty::Kind::Record)) {
            error("cannot access member '" + e.member + "' of non-record type " + base.name());
            m_result = makeError();
            return;
        }

        const Fields *fields = lookupRecord(base.record);
        if (!fields) {
            m_result = makeError();
            return;
        }
        for (const auto &[name, type] : *fields)
            if (name == e.member) {
                m_result = type;
                print(dimText("access ") + nameText(base.record + "." + e.member) + dimText(" : ")
                      + typeText(m_result.name()));
                return;
            }

        error("record '" + base.record + "' has no field '" + e.member + "'");
        m_result = makeError();
    }

    void TypeResolver::visit(IndexExpr &e) {
        const Ty base = typeOf(e.base);
        const Ty index = typeOf(e.index);

        if (!makeFloat().accepts(index))
            error("array index must be float, got " + index.name());
        if (base.isError()) {
            m_result = makeError();
            return;
        }
        if (!base.is(Ty::Kind::Array)) {
            error("cannot index non-array type " + base.name());
            m_result = makeError();
            return;
        }

        m_result = *base.elem;
        print(dimText("index ") + typeText(base.name()) + dimText(" : ") + typeText(m_result.name()));
    }

    // The grammar only allows `&` on a call argument, so this is always the
    // address a `&T` parameter will hold -- and that has to be somewhere real.
    void TypeResolver::visit(RefExpr &e) {
        const Ty operand = typeOf(e.operand);
        if (e.operand && !isLValue(*e.operand)) {
            error("only a variable, a field or an array element can be passed by reference");
            m_result = makeError();
            return;
        }
        m_result = operand.isError() ? makeError() : makeRef(operand);
        print(dimText("ref : ") + typeText(m_result.name()));
    }

    // ---- Stmt ----------------------------------------------------------------

    void TypeResolver::visit(CompoundStmt &s) {
        pushScope("block");
        for (const StmtPtr &b : s.body)
            walk(b);
        popScope();
    }

    void TypeResolver::visit(VarDeclStmt &s) { walk(s.decl); }
    void TypeResolver::visit(ExprStmt &s) { typeOf(s.expr); }

    void TypeResolver::visit(ReturnStmt &s) {
        const Ty value = typeOf(s.expr);
        print(dimText("return ") + typeText(value.name()) + dimText(" (expected ")
              + typeText(m_returnType.name()) + dimText(")"));
        if (m_returnType.is(Ty::Kind::Void) && !value.is(Ty::Kind::Void)) {
            error("function '" + m_function + "' returns void, but a value was returned");
            return;
        }
        if (!m_returnType.accepts(value))
            error("function '" + m_function + "' must return " + m_returnType.name() + ", got "
                  + value.name());
    }

    void TypeResolver::visit(AssignStmt &s) {
        const Ty target = typeOf(s.target);
        const Ty value = typeOf(s.value);

        print(dimText("assign ") + typeText(target.name()) + dimText(" = ")
              + typeText(value.name()));
        if (s.target && !isLValue(*s.target))
            error("left-hand side of an assignment must be a variable, a field or an array element");
        else if (!target.accepts(value))
            error("cannot assign " + value.name() + " to " + target.name());
    }

    // if / while / for all share the same rule: the condition is a bool, no
    // implicit truthiness.
    void TypeResolver::visit(IfStmt &s) {
        checkCondition(s.cond, "if");
        walk(s.then);
        walk(s.otherwise);
    }

    void TypeResolver::visit(WhileStmt &s) {
        checkCondition(s.cond, "while");
        walk(s.body);
    }

    void TypeResolver::visit(ForStmt &s) {
        pushScope("for");
        walk(s.init);
        if (s.cond) // `for (;;)` has no condition to check
            checkCondition(s.cond, "for");
        walk(s.step);
        walk(s.body);
        popScope();
    }

    // ---- Decl ----------------------------------------------------------------

    void TypeResolver::visit(VarDecl &d) {
        const Ty declared = typeOfType(d.type);
        if (declared.is(Ty::Kind::Void))
            error("variable '" + d.name + "' cannot have type void");

        if (d.init) {
            const Ty init = typeOf(d.init);
            if (!declared.accepts(init))
                error("cannot initialise '" + d.name + "' of type " + declared.name() + " with "
                      + init.name());
        }
        declare(d.name, declared);
    }

    void TypeResolver::visit(FunDecl &d) {
        const FunSig &sig = m_functions.at(d.name);

        const Ty outerReturn = m_returnType;
        const std::string outerFunction = m_function;
        m_returnType = sig.ret;
        m_function = d.name;

        print(dimText("function ") + nameText(d.name) + dimText(" -> ") + typeText(sig.ret.name()));

        pushScope("function");
        for (std::size_t i = 0; i < d.params.size(); ++i) {
            const Ty type = sig.params[i];
            if (type.is(Ty::Kind::Void))
                error("parameter '" + d.params[i].name + "' of '" + d.name
                      + "' cannot have type void");
            // Inside the body a reference reads like the value it points to, so
            // `a.f` works unchanged; only the call has to spell out `&`.
            declare(d.params[i].name, type.is(Ty::Kind::Ref) ? *type.elem : type);
        }

        // Params and the body's top-level locals share one scope, matching how
        // Resolver treats a local shadowing a parameter as a redefinition.
        if (auto *body = dynamic_cast<CompoundStmt *>(d.body.get())) {
            for (const StmtPtr &b : body->body)
                walk(b);
        } else {
            walk(d.body);
        }
        popScope();

        m_returnType = outerReturn;
        m_function = outerFunction;
    }

    // Field types were resolved in collectSignatures; all that is left is to
    // reject the ones no value can have.
    void TypeResolver::visit(RecordDecl &d) {
        const Fields *fields = lookupRecord(d.name);
        if (!fields)
            return;
        print(dimText("record ") + nameText(d.name));
        for (const auto &[name, type] : *fields) {
            print(dimText("  field ") + nameText(name) + dimText(" : ") + typeText(type.name()));
            if (type.is(Ty::Kind::Void))
                error("field '" + name + "' of record '" + d.name + "' cannot have type void");
        }
    }
}
