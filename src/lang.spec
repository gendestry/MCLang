namespace Basic

# The tree root is a single expression.
program Decl[]

category Type {
    AtomicType { string prim }        # "float" | "bool" | "void"
    NamedType  { string name }        # namedtype : a record name
}

record Param {
    Type   type
    string name
}

# ---- EXPR : operator cascade + atoms ------------------------------------
category Expr {
    NumberExpr { f64 value }           # NUM (may carry a fractional part)
    BoolExpr   { bool value }          # TRUE | FALSE
    StringExpr { string value }        # STRING_LIT (quotes stripped)
    BinaryExpr { string op; Expr lhs; Expr rhs }   # + - * /  (left-folded)
    CallExpr   { string callee; Expr[] args }      # funcall
    NamedExpr   { string callee; Expr[] args }      # funcall
    AccessExpr { Expr base; string member }        # accessexpr : p.x (left-folded)
    UnaryExpr  { string op; Expr operand }         # unaryexpr : !x
}

# ---- STMT : compound / decl / expr / assign ---------------------------
category Stmt {
    CompoundStmt { Stmt[] body }       # compstmt : { stmt* }
    VarDeclStmt  { Decl decl }         # vardeclstmt : vardecl
    ExprStmt     { Expr expr }         # exprstmt : expr ;
    ReturnStmt     { Expr expr }         # exprstmt : expr ;
    AssignStmt   { Expr target; Expr value }   # assignstmt : expr = expr ;
    IfStmt       { Expr cond; Stmt then; Stmt otherwise }   # otherwise null when no else
    WhileStmt    { Expr cond; Stmt body }
    ForStmt      { Stmt init; Expr cond; Stmt step; Stmt body }  # init null for `for (;`
}

# ---- DECL : var / fun -------------------------------------------------
category Decl {
    VarDecl { Type type; string name; Expr init }   # init null when absent
    FunDecl { Type returnType; string name; Param[] params; Stmt body }
    RecordDecl { string name; Decl[] fields }       # recdecl : fields are VarDecls
}