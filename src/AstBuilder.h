//
// Created by bobi on 6. 9. 26.
//

#pragma once
#include "LangAst.h"
#include "Syntax/Node.h"

namespace Basic {
    // CST -> AST lowering for lang.syn. Walks the homogeneous Node tree the
    // Engine produces and builds typed Basic:: nodes. No evaluation happens here.
    //
    // Reminder on how the Engine flattens a rule body into `kids`:
    //   Terminal/Literal -> leaf;  RuleRef -> child node;  Seq/Star/Plus/Optional
    //   and ( ) groups flatten into the parent;  Choice -> only the winning
    //   alternative. So every build* below reads `kids` BY POSITION.
    class AstBuilder {
    public:
        Program buildProgram(const Parsing::Syntax::Node &entry); // entry : decl+

    private:
        using Node = Parsing::Syntax::Node;

        DeclPtr buildDecl(const Node &decl);          // decl : recdecl | fundecl | vardecl
        DeclPtr buildVarDecl(const Node &vardecl);    // vardecl : type IDENTIFIER (= expr)? ;
        DeclPtr buildFunDecl(const Node &fundecl);    // fundecl : type IDENTIFIER ( params ) compstmt
        DeclPtr buildRecDecl(const Node &recdecl);    // recdecl : record IDENTIFIER { vardecl+ } ;
        TypePtr buildType(const Node &type);          // type : (primtype | namedtype) [N]*
        StmtPtr buildStmt(const Node &stmt);          // stmt : any of the eight statement forms
        StmtPtr buildIf(const Node &ifstmt);          // ifstmt : if ( expr ) stmt (else stmt)?
        StmtPtr buildWhile(const Node &whilestmt);    // whilestmt : while ( expr ) stmt
        StmtPtr buildFor(const Node &forstmt);        // forstmt : for ( init cond ; step ) stmt
        StmtPtr buildCompound(const Node &compstmt);  // compstmt : { stmt* }
        StmtPtr buildAssign(const Node &assignstmt);  // assignstmt : expr = expr ;
        StmtPtr buildReturn(const Node &returnstmt);  // returnstmt : return expr? ;
        ExprPtr buildExpr(const Node &node);          // expr cascade, incl. unary + access
        ExprPtr foldTail(ExprPtr lhs, const Node &tail);
    };
}
