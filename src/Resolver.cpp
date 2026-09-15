//
// Created by bobi on 6. 9. 26.
//
//  Scopes are a stack of name -> Kind maps. Declarations are added as they are
//  reached, never hoisted, so "declared before use" is enforced simply by the
//  order in which the walk visits nodes.

#include "Resolver.h"

#include <iostream>

#include "Utils/Colors/Font.h"

namespace Basic {
    const char *Resolver::kindName(Kind kind) {
        switch (kind) {
        case Kind::Var: return "variable";
        case Kind::Fun: return "function";
        case Kind::Record: return "record";
        }
        return "name";
    }

    namespace {
        // Same colour vocabulary as AstPrinter: magenta for a kind, yellow for a
        // name, dim for structure, green/red for the outcome of a lookup.
        std::string kindText(const char *kind) {
            return Utils::Font::colorMagenta + std::string(kind) + Utils::Font::colorReset;
        }
        std::string nameText(const std::string &name) {
            return Utils::Font::colorYellow + "'" + name + "'" + Utils::Font::colorReset;
        }
        std::string okText(const std::string &text) {
            return Utils::Font::colorGreen + text + Utils::Font::colorReset;
        }
        std::string badText(const std::string &text) {
            return Utils::Font::colorRed + text + Utils::Font::colorReset;
        }
        std::string dimText(const std::string &text) {
            return Utils::Font::colorDim + text + Utils::Font::colorReset;
        }
    }

    void Resolver::print(const std::string &message) const {
        if (!m_print)
            return;
        // Indent one level per open scope, so the output mirrors the nesting.
        std::cout << std::string(m_scopes.size() * 2, ' ') << message << std::endl;
    }

    void Resolver::pushScope(const char *what) {
        m_scopes.emplace_back();
        print(dimText("+ scope ") + dimText(what)); // after the push: contents indent under it
    }

    void Resolver::popScope() {
        print(dimText("- scope"));
        m_scopes.pop_back();
    }

    const Resolver::VarRef *Resolver::declOf(const NamedExpr *use) const {
        auto it = m_names.find(use);
        return it == m_names.end() ? nullptr : &it->second;
    }

    const FunDecl *Resolver::declOf(const CallExpr *call) const {
        auto it = m_calls.find(call);
        return it == m_calls.end() ? nullptr : it->second;
    }

    bool Resolver::resolve(const Program &program) {
        m_errors.clear();
        m_scopes.clear();
        m_names.clear();
        m_calls.clear();
        pushScope("global");

        for (const DeclPtr &d : program)
            walk(d);

        popScope();
        return m_errors.empty();
    }

    // Shadowing an outer scope is fine; colliding within one scope is not.
    void Resolver::declare(const std::string &name, Entry entry) {
        Scope &scope = m_scopes.back();
        auto [it, inserted] = scope.emplace(name, entry);
        if (!inserted) {
            print(badText("declare ") + kindText(kindName(entry.kind)) + " " + nameText(name) + " "
                  + badText("REDEFINITION"));
            error("redefinition of '" + name + "' (already declared as a "
                  + kindName(it->second.kind) + " in this scope)");
            return;
        }
        print(okText("declare ") + kindText(kindName(entry.kind)) + " " + nameText(name)
              + dimText(" @ depth " + std::to_string(m_scopes.size() - 1)));
    }

    const Resolver::Entry *Resolver::lookup(const std::string &name, std::size_t *depth) const {
        for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope) {
            auto it = scope->find(name);
            if (it != scope->end()) {
                if (depth)
                    *depth = static_cast<std::size_t>(m_scopes.rend() - scope) - 1;
                return &it->second;
            }
        }
        return nullptr;
    }

    const Resolver::Entry *Resolver::use(const std::string &name, Kind expected, const char *what) {
        std::size_t depth = 0;
        const Entry *entry = lookup(name, &depth);
        const std::string head =
            "resolve " + kindText(what) + " " + nameText(name) + dimText(" -> ");

        if (!entry) {
            print(head + badText("UNDECLARED"));
            error("use of undeclared " + std::string(what) + " '" + name + "'");
            return nullptr;
        }
        if (entry->kind != expected) {
            print(head + badText(std::string("found a ") + kindName(entry->kind)));
            error("'" + name + "' is a " + kindName(entry->kind) + ", not a " + what);
            return nullptr;
        }
        print(head + okText("ok") + dimText(" @ depth " + std::to_string(depth)));
        return entry;
    }

    // ---- Type ----------------------------------------------------------------

    void Resolver::visit(AtomicType &) {} // float / bool / void are built in

    void Resolver::visit(NamedType &t) { use(t.name, Kind::Record, "record type"); }

    void Resolver::visit(ArrayType &t) { walk(t.elem); }
    void Resolver::visit(RefType &t) { walk(t.elem); }

    // ---- Expr ----------------------------------------------------------------

    void Resolver::visit(NumberExpr &) {}
    void Resolver::visit(BoolExpr &) {}
    void Resolver::visit(StringExpr &) {}

    void Resolver::visit(BinaryExpr &e) {
        walk(e.lhs);
        walk(e.rhs);
    }

    void Resolver::visit(UnaryExpr &e) { walk(e.operand); }

    void Resolver::visit(CallExpr &e) {
        if (const Entry *fun = use(e.callee, Kind::Fun, "function"))
            m_calls[&e] = fun->fun;
        for (const ExprPtr &a : e.args)
            walk(a);
    }

    void Resolver::visit(NamedExpr &e) {
        if (const Entry *var = use(e.callee, Kind::Var, "variable"))
            m_names[&e] = {var->var, var->param};
    }

    // Only the base is a name; the member is a field of whatever record the base
    // turns out to be, which needs types to check.
    void Resolver::visit(AccessExpr &e) { walk(e.base); }

    void Resolver::visit(IndexExpr &e) {
        walk(e.base);
        walk(e.index);
    }

    void Resolver::visit(RefExpr &e) { walk(e.operand); }

    // ---- Stmt ----------------------------------------------------------------

    void Resolver::visit(CompoundStmt &s) {
        pushScope("block");
        for (const StmtPtr &b : s.body)
            walk(b);
        popScope();
    }

    void Resolver::visit(VarDeclStmt &s) { walk(s.decl); }
    void Resolver::visit(ExprStmt &s) { walk(s.expr); }
    void Resolver::visit(ReturnStmt &s) { walk(s.expr); }

    void Resolver::visit(AssignStmt &s) {
        walk(s.target);
        walk(s.value);
    }

    void Resolver::visit(IfStmt &s) {
        walk(s.cond);
        walk(s.then);
        walk(s.otherwise);
    }

    void Resolver::visit(WhileStmt &s) {
        walk(s.cond);
        walk(s.body);
    }

    // The init declares into a scope of the loop's own, so `i` dies with the loop
    // and two sibling loops may both declare one.
    void Resolver::visit(ForStmt &s) {
        pushScope("for");
        walk(s.init);
        walk(s.cond);
        walk(s.step);
        walk(s.body);
        popScope();
    }

    // ---- Decl ----------------------------------------------------------------

    // The initialiser is resolved *before* the name is declared, so `float x = x;`
    // reports x as undeclared instead of silently reading itself.
    void Resolver::visit(VarDecl &d) {
        walk(d.type);
        walk(d.init);
        declare(d.name, {Kind::Var, &d});
    }

    void Resolver::visit(FunDecl &d) {
        print(dimText("function ") + nameText(d.name));
        declare(d.name, {Kind::Fun, nullptr, nullptr, &d}); // before the body, so recursion resolves
        walk(d.returnType);

        pushScope("function");
        for (Param &p : d.params) {
            walk(p.type);
            declare(p.name, {Kind::Var, nullptr, &p});
        }

        // Walk the body's statements directly in the parameter scope rather than
        // letting CompoundStmt open a nested one, so a local shadowing a parameter
        // is a redefinition (as in C) instead of silently allowed.
        if (auto *body = dynamic_cast<CompoundStmt *>(d.body.get())) {
            for (const StmtPtr &b : body->body)
                walk(b);
        } else {
            walk(d.body);
        }
        popScope();
    }

    void Resolver::visit(RecordDecl &d) {
        print(dimText("record ") + nameText(d.name));
        declare(d.name, {Kind::Record}); // before the fields, so a record may name itself

        // Field names live in the record, not in the enclosing scope, so they are
        // checked against each other rather than declared into a Scope.
        Scope fields;
        for (const DeclPtr &f : d.fields) {
            auto *field = dynamic_cast<VarDecl *>(f.get());
            if (!field)
                continue; // the grammar only allows vardecl inside a record

            walk(field->type);
            walk(field->init);
            if (!fields.emplace(field->name, Entry{Kind::Var}).second)
                error("duplicate field '" + field->name + "' in record '" + d.name + "'");
        }
    }
}
