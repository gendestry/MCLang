//
// Created by bobi on 6. 9. 26.
//
//  One build* method per lang.syn rule. Each reads its CST node's `kids` by
//  position and packs them into typed Basic:: AST nodes.

#include "AstBuilder.h"

#include <stdexcept>
#include <string>

namespace Basic {
    // entry : decl+        kids = [decl, ...]
    Program AstBuilder::buildProgram(const Node &entry) {
        Program program;
        for (const Node &decl : entry.kids)
            program.push_back(buildDecl(decl));
        return program;
    }

    // decl : recdecl | fundecl | vardecl        kids = [ the chosen rule node ]
    DeclPtr AstBuilder::buildDecl(const Node &decl) {
        const Node &c = decl.kids[0];
        if (c.rule == "vardecl")
            return buildVarDecl(c);
        if (c.rule == "fundecl")
            return buildFunDecl(c);
        if (c.rule == "recdecl")
            return buildRecDecl(c);
        throw std::runtime_error("buildDecl: unexpected '" + c.rule + "'");
    }

    // vardecl : type IDENTIFIER (EQ expr)? SEMIC
    //   kids = [type, IDENTIFIER, EQ, expr, SEMIC] | [type, IDENTIFIER, SEMIC]
    DeclPtr AstBuilder::buildVarDecl(const Node &v) {
        auto d = std::make_unique<VarDecl>();
        d->type = buildType(v.kids[0]);
        d->name = v.kids[1].token->value;
        if (v.kids.size() > 2 && v.kids[2].isTokenName("EQ"))
            d->init = buildExpr(v.kids[3]);
        return d;
    }

    // fundecl : type IDENTIFIER LPAREN (type IDENTIFIER (COMMA type IDENTIFIER)*)?
    //           RPAREN compstmt
    //   kids = [type, IDENTIFIER, LPAREN, <flat params...>, RPAREN, compstmt]
    DeclPtr AstBuilder::buildFunDecl(const Node &f) {
        auto d = std::make_unique<FunDecl>();
        d->returnType = buildType(f.kids[0]);
        d->name = f.kids[1].token->value;

        // Params are a flat run of (type IDENTIFIER) pairs -- COMMA and the
        // parens are leaves we skip -- ending at the compstmt body.
        for (std::size_t i = 3; i < f.kids.size(); ++i) {
            const Node &k = f.kids[i];
            if (k.rule == "compstmt") {
                d->body = buildCompound(k);
                break;
            }
            if (k.rule == "type")
                d->params.push_back({buildType(k), f.kids[i + 1].token->value});
        }
        return d;
    }

    // recdecl : RECORD IDENTIFIER LCURLY vardecl+ RCURLY SEMIC
    //   kids = [RECORD, IDENTIFIER, LCURLY, vardecl..., RCURLY, SEMIC]
    DeclPtr AstBuilder::buildRecDecl(const Node &r) {
        auto d = std::make_unique<RecordDecl>();
        d->name = r.kids[1].token->value; // the IDENTIFIER after `record`
        for (const Node &k : r.kids)
            if (k.rule == "vardecl")
                d->fields.push_back(buildVarDecl(k));
        return d;
    }

    // type : (primtype | namedtype) (LBRACKET NUM RBRACKET)*
    //   kids = [ the chosen rule node, LBRACKET, NUM, RBRACKET, ... ]
    TypePtr AstBuilder::buildType(const Node &type) {
        const Node &inner = type.kids[0];
        TypePtr base;

        if (inner.rule == "primtype") { // primtype : FLOAT | BOOL | VOID   kids = [leaf]
            auto t = std::make_unique<AtomicType>();
            t->prim = inner.kids[0].token->value; // "float" | "bool" | "void"
            base = std::move(t);
        } else if (inner.rule == "namedtype") { // namedtype : IDENTIFIER   kids = [leaf]
            auto t = std::make_unique<NamedType>();
            t->name = inner.kids[0].token->value;
            base = std::move(t);
        } else {
            throw std::runtime_error("buildType: unexpected '" + inner.rule + "'");
        }

        // The lengths are written outermost first, so wrap from the last one in:
        // float[2][3] -> Array(Array(float, 3), 2).
        std::vector<std::size_t> lengths;
        for (std::size_t i = 1; i < type.kids.size(); ++i)
            if (type.kids[i].isTokenName("NUM")) {
                const std::string &raw = type.kids[i].token->value;
                if (raw.find('.') != std::string::npos || std::stoull(raw) == 0)
                    throw std::runtime_error("array length must be a positive integer, got '" + raw
                                             + "'");
                lengths.push_back(std::stoull(raw));
            }
        for (auto length = lengths.rbegin(); length != lengths.rend(); ++length) {
            auto t = std::make_unique<ArrayType>();
            t->elem = std::move(base);
            t->length = *length;
            base = std::move(t);
        }
        return base;
    }

    // stmt : compstmt | vardeclstmt | exprstmt | assignstmt | returnstmt
    //   kids = [ the chosen alternative ]
    StmtPtr AstBuilder::buildStmt(const Node &stmt) {
        const Node &c = stmt.kids[0];

        if (c.rule == "compstmt")
            return buildCompound(c);

        if (c.rule == "vardeclstmt") { // vardeclstmt : vardecl
            auto s = std::make_unique<VarDeclStmt>();
            s->decl = buildVarDecl(c.kids[0]);
            return s;
        }

        if (c.rule == "exprstmt") { // exprstmt : expr SEMIC
            auto s = std::make_unique<ExprStmt>();
            s->expr = buildExpr(c.kids[0]);
            return s;
        }

        if (c.rule == "assignstmt")
            return buildAssign(c);

        if (c.rule == "returnstmt")
            return buildReturn(c);

        if (c.rule == "ifstmt")
            return buildIf(c);

        if (c.rule == "whilestmt")
            return buildWhile(c);

        if (c.rule == "forstmt")
            return buildFor(c);

        throw std::runtime_error("buildStmt: unexpected '" + c.rule + "'");
    }

    // ifstmt : IF LPAREN expr RPAREN stmt (ELSE stmt)?
    //   kids = [IF, LPAREN, expr, RPAREN, stmt] (+ [ELSE, stmt])
    // A dangling `else` binds to the nearest `if`: the optional is greedy, so the
    // inner ifstmt claims it before the outer one gets a chance.
    StmtPtr AstBuilder::buildIf(const Node &n) {
        auto s = std::make_unique<IfStmt>();
        s->cond = buildExpr(n.kids[2]);
        s->then = buildStmt(n.kids[4]);
        if (n.kids.size() > 5) // [5] is the ELSE leaf, [6] the else branch
            s->otherwise = buildStmt(n.kids[6]);
        return s;
    }

    // whilestmt : WHILE LPAREN expr RPAREN stmt
    //   kids = [WHILE, LPAREN, expr, RPAREN, stmt]
    StmtPtr AstBuilder::buildWhile(const Node &n) {
        auto s = std::make_unique<WhileStmt>();
        s->cond = buildExpr(n.kids[2]);
        s->body = buildStmt(n.kids[4]);
        return s;
    }

    // forstmt : FOR LPAREN forinit expr SEMIC forstep RPAREN stmt
    //   kids = [FOR, LPAREN, forinit, expr, SEMIC, forstep, RPAREN, stmt]
    //   forinit : vardecl | assignstmt | SEMIC   (the two rule forms carry their
    //             own SEMIC, so an empty init is just a bare `;` leaf)
    //   forstep : expr EQ expr                   (no SEMIC -- RPAREN follows)
    StmtPtr AstBuilder::buildFor(const Node &n) {
        auto s = std::make_unique<ForStmt>();

        const Node &init = n.kids[2].kids[0];
        if (init.rule == "vardecl") {
            auto d = std::make_unique<VarDeclStmt>();
            d->decl = buildVarDecl(init);
            s->init = std::move(d);
        } else if (init.rule == "assignstmt") {
            s->init = buildAssign(init);
        } // else: the bare SEMIC form, init stays null

        s->cond = buildExpr(n.kids[3]);

        const Node &step = n.kids[5]; // forstep : expr EQ expr
        auto st = std::make_unique<AssignStmt>();
        st->target = buildExpr(step.kids[0]);
        st->value = buildExpr(step.kids[2]);
        s->step = std::move(st);

        s->body = buildStmt(n.kids[7]);
        return s;
    }

    // compstmt : LCURLY stmt* RCURLY   -> collect the stmt kids
    StmtPtr AstBuilder::buildCompound(const Node &comp) {
        auto s = std::make_unique<CompoundStmt>();
        for (const Node &k : comp.kids)
            if (k.rule == "stmt")
                s->body.push_back(buildStmt(k));
        return s;
    }

    // assignstmt : expr EQ expr SEMIC     kids = [expr, EQ, expr, SEMIC]
    StmtPtr AstBuilder::buildAssign(const Node &a) {
        auto s = std::make_unique<AssignStmt>();
        s->target = buildExpr(a.kids[0]);
        s->value = buildExpr(a.kids[2]);
        return s;
    }

    // returnstmt : RETURN expr? SEMIC
    //   kids = [RETURN, expr, SEMIC]  or  [RETURN, SEMIC]  (bare return)
    StmtPtr AstBuilder::buildReturn(const Node &ret) {
        auto s = std::make_unique<ReturnStmt>();
        if (ret.kids[1].rule == "expr") // optional value present
            s->expr = buildExpr(ret.kids[1]);
        return s;
    }

    // The expression cascade, collapsed into Number/Binary/Call/Named nodes.
    ExprPtr AstBuilder::buildExpr(const Node &node) {
        // Every binary precedence level has the same shape: `head tail?`, where
        // the tail is the right-recursive n_* rule foldTail() flattens.
        if (node.rule == "expr" || node.rule == "orexpr" || node.rule == "andexpr" ||
            node.rule == "eqexpr" || node.rule == "relexpr" || node.rule == "plusexpr") {
            ExprPtr head = buildExpr(node.kids[0]);
            return node.kids.size() == 2 ? foldTail(std::move(head), node.kids[1])
                                         : std::move(head);
        }

        if (node.rule == "mulexpr") // mulexpr : unaryexpr   (single child)
            return buildExpr(node.kids[0]);

        if (node.rule == "unaryexpr") { // NOT unaryexpr | accessexpr
            if (node.kids[0].isTokenName("NOT")) {
                auto u = std::make_unique<UnaryExpr>();
                u->op = node.kids[0].token->value;
                u->operand = buildExpr(node.kids[1]); // right-recursive: !!x
                return u;
            }
            return buildExpr(node.kids[0]);
        }

        if (node.rule == "accessexpr") { // atom ((DOT IDENTIFIER) | (LBRACKET expr RBRACKET))*
            // kids = [atom, DOT, IDENTIFIER, LBRACKET, expr, RBRACKET, ...].
            // Fold each `.member` and `[index]` in, left-associative:
            // a[i].x -> Access(Index(a, i), x).
            ExprPtr base = buildExpr(node.kids[0]);
            for (std::size_t i = 1; i < node.kids.size(); ++i) {
                const Node &k = node.kids[i];
                if (k.isTokenName("IDENTIFIER")) {
                    auto a = std::make_unique<AccessExpr>();
                    a->base = std::move(base);
                    a->member = k.token->value;
                    base = std::move(a);
                } else if (k.rule == "expr") {
                    auto ix = std::make_unique<IndexExpr>();
                    ix->base = std::move(base);
                    ix->index = buildExpr(k);
                    base = std::move(ix);
                }
            }
            return base;
        }

        if (node.rule == "atom") { // NUM | STRING_LIT | TRUE | FALSE | group | funcall | namedexpr
            const Node &k = node.kids[0];
            if (k.isTokenName("NUM")) {
                auto n = std::make_unique<NumberExpr>();
                n->value = std::stod(k.token->value);
                return n;
            }

            if (k.isTokenName("TRUE") || k.isTokenName("FALSE")) {
                auto n = std::make_unique<BoolExpr>();
                n->value = k.isTokenName("TRUE");
                return n;
            }

            if (k.isTokenName("STRING_LIT")) {
                // The token still carries its delimiters; the value does not.
                auto n = std::make_unique<StringExpr>();
                const std::string &raw = k.token->value;
                n->value = raw.substr(1, raw.size() - 2);
                return n;
            }
            return buildExpr(k); // group, funcall or namedexpr node
        }

        if (node.rule == "namedexpr") { // namedexpr : IDENTIFIER
            auto n = std::make_unique<NamedExpr>();
            n->callee = node.kids[0].token->value;
            return n;
        }

        if (node.rule == "group") // group : LPAREN expr RPAREN
            return buildExpr(node.kids[1]);

        if (node.rule == "funcall") { // IDENTIFIER LPAREN (expr (COMMA expr)*)? RPAREN
            auto call = std::make_unique<CallExpr>();
            call->callee = node.kids[0].token->value;
            for (std::size_t i = 2; i < node.kids.size(); ++i)
                if (node.kids[i].rule == "expr")
                    call->args.push_back(buildExpr(node.kids[i]));
            return call;
        }

        throw std::runtime_error("buildExpr: unhandled rule '" + node.rule + "'");
    }

    // <op> operand tail?  -> left-associative BinaryExpr chain. One tail rule per
    // precedence level (n_orexpr .. n_mulexpr); they all share this shape.
    // The CST leans right (n_plusexpr nests on the right); folding here from the
    // left keeps '+' '-' '*' '/' left-associative.
    ExprPtr AstBuilder::foldTail(ExprPtr lhs, const Node &tail) {
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = tail.kids[0].token->value;
        bin->lhs = std::move(lhs);
        bin->rhs = buildExpr(tail.kids[1]);
        return tail.kids.size() == 3 ? foldTail(std::move(bin), tail.kids[2])
                                     : std::move(bin);
    }
}
