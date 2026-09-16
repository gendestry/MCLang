# MCLang

MCLang is a small C-like language that compiles to a Minecraft Java Edition
datapack (1.21+). This page describes the syntax as it is defined by
`src/lang.tok` (tokens) and `src/lang.syn` (grammar).

```c
record Point {
  float x;
  float y;
};

float lengthSquared(Point* p) {
  return p->x * p->x + p->y * p->y;
}

float main() {
  Point p;
  p.x = 3.0;
  p.y = 4.0;
  cmd("say {}", lengthSquared(&p));   // prints 25 in chat
  return lengthSquared(&p);
}
```

## Contents

- [Running a program](#running-a-program)
- [Lexical structure](#lexical-structure)
- [Types](#types)
- [Declarations](#declarations)
- [Statements](#statements)
- [Expressions](#expressions)
- [Pointers](#pointers)
- [Minecraft commands](#minecraft-commands)
- [Entry points](#entry-points)
- [Limitations](#limitations)
- [Grammar](#grammar)

---

## Running a program

The compiler reads `input.txt` from its working directory, together with
`lang.tok` and `lang.syn`, and writes `datapack/`:

```bash
cd cmake-build-debug
./MCLang
```

It type-checks the program, runs `main` in a built-in interpreter (printing
`main returned …`), and generates the datapack. To play it:

```bash
cp -r datapack ~/.minecraft/saves/<World>/datapacks/mclang
```

```
/reload
/function mcl:run
```

---

## Lexical structure

### Comments

```c
// to the end of the line
/* across
   lines */
```

### Identifiers

Letters and digits only: `[A-Za-z0-9]+`. **Underscores are not allowed**
(`my_x` is a tokenizer error), so use camelCase: `myX`.

### Keywords

```
record  return  for  while  if  else
true  false  null
float  bool  void  string  char
```

`string` and `char` are reserved but have no use yet.

### Literals

| Kind    | Examples              | Notes                                         |
|---------|-----------------------|-----------------------------------------------|
| Number  | `0` `42` `3.5` `0.25` | Always a `float`. No exponent, no leading `.` |
| Boolean | `true` `false`        |                                               |
| Null    | `null`                | The pointer that points nowhere               |
| String  | `"say hi"`            | Only as the command in `cmd(...)`; no escapes |

There are **no negative literals**: `-1.0` is a syntax error. Write
`0.0 - 1.0`.

---

## Types

| Type        | Meaning                                     | Size in slots        |
|-------------|---------------------------------------------|----------------------|
| `float`     | A number (the only number type)             | 1                    |
| `bool`      | `true` / `false`                            | 1                    |
| `void`      | No value; only as a function's return type  | –                    |
| `Name`      | A record declared with `record Name { … };` | sum of its fields    |
| `T[N]`      | An array of `N` elements of `T`             | `N` × size of `T`    |
| `T*`        | A pointer to a `T`                          | 1                    |

Types compose left to right. The stars come before the array lengths:

```c
float[3][2] grid;   // 3 rows, each a float[2]
float*[4] rows;     // 4 pointers to float
float** pp;         // pointer to pointer to float
Point*[8] ptrs;     // 8 pointers to Point
```

In `float[3][2]` the **outer length comes first**, so `grid[i]` is a
`float[2]` and `grid[i][j]` is a `float`.

**Numbers are fixed point** in the datapack: every `float` is stored as an
integer scoreboard value × 1000. So there are three decimal places, and values
must stay within roughly ±2,147,483.

---

## Declarations

A program is a list of records, functions and global variables. **Everything
must be declared before it is used**, including functions: a function can call
itself or anything declared above it, but not a function declared further down.

### Variables

```c
float speed;                // a global: starts at 0
float main() {
  float x = 1.5;            // a local, with an initialiser
  bool done;                // starts at false (0)
  float[10] values;         // all elements start at 0
  return x;
}
```

- Globals **cannot have an initialiser** yet (`float g = 1.0;` at top level is
  an error). Assign them inside a function instead.
- A local cannot have the same name as a parameter of its function.
- A name can shadow one from an outer scope.

### Functions

```c
float add(float a, float b) {
  return a + b;
}

void reset(float* p) {
  *p = 0.0;
}
```

- Parameters are passed **by value**: a record or an array argument is copied
  whole. To modify the caller's variable, pass a pointer.
- A function can **only return a one-slot value**: `float`, `bool`, a pointer,
  or nothing (`void`). Returning a record or array is not supported. Write into
  an out-pointer instead.
- Recursion works.

### Records

```c
record Segment {
  Point head;
  Point tail;
  bool closed;
};                          // note the semicolon
```

Fields are plain variable declarations (without initialisers). Records can
contain other records, arrays and pointers.

---

## Statements

```c
{ … }                                   // block, opens a scope
float x = 1.0;                          // declaration
x = x + 1.0;                            // assignment
f(x);                                   // expression statement
return x;                               // return (return; in void functions)

if (x < 3.0) x = 3.0;
if (done) { … } else { … }

while (i < 10.0) { i = i + 1.0; }

for (float i = 0.0; i < 10.0; i = i + 1.0) { … }
for (; i < 10.0; i = i + 1.0) { … }     // empty init
```

- Conditions **must be `bool`**. `if (x)` with a float, or `if (p)` with a
  pointer, is a type error. Write `x != 0.0`, `p != null`.
- A `for` loop **needs a condition**, and its step is an assignment
  (`i = i + 1.0`). There is no `i++`, `+=`, `break` or `continue`.
- The left side of `=` must be something in memory: a variable, a field, an
  element, or `*p`.

---

## Expressions

### Operators

From **lowest to highest** precedence (all binary operators are left-associative):

| Operators            | Operands                                      | Result  |
|----------------------|-----------------------------------------------|---------|
| `\|\|`               | `bool`                                        | `bool`  |
| `&&`                 | `bool`                                        | `bool`  |
| `==` `!=`            | two values of the same type, or pointer/`null` | `bool` |
| `<` `>` `<=` `>=`    | `float`, or two pointers of the same type     | `bool`  |
| `+` `-`              | `float`, or pointer arithmetic                | varies  |
| `*` `/`              | `float`                                       | `float` |
| `!` `*` `&` (prefix) | `!bool`, `*pointer`, `&lvalue`                | varies  |
| `.` `->` `[ ]` `( )` | field, field through pointer, index, call     | varies  |

- `&&` and `||` short-circuit: the right side runs only when needed.
- `==` works on records too (field by field in the interpreter), but comparing
  records or arrays is **not supported in the datapack**.
- Division by zero stops the interpreter; in the datapack it prints
  `MCLang: division by zero`.
- There is no `%` and no unary minus.

### Access

```c
p.x            // field of a record
p->x           // field through a pointer, same as (*p).x
a[i]           // element i (i is a float; 1 and 1.0 are the same)
grid[i][j]     // nested
f(a, b)        // call
```

Indexes are **not bounds-checked**, and a fractional index such as `1.5` is not
rejected. It reads the wrong slot.

---

## Pointers

Pointers work like in C.

```c
float x = 3.0;
float* p = &x;        // address of x
*p = *p * 2.0;        // x is now 6

float[4] a;
float* q = a;         // an array becomes a pointer to its first element
q[2] = 7.0;           // a[2] = 7
float* r = a + 3.0;   // &a[3]
float n = r - a;      // 3, the number of elements between them

Point* pt = null;
if (pt == null) { … }
```

### Operations

| Expression     | Meaning                                                   |
|----------------|-----------------------------------------------------------|
| `&x`           | Address of a variable, field, element or `*p`             |
| `*p`           | The value `p` points at (readable and assignable)         |
| `p[i]`         | Element `i` counting from `p`, same as `*(p + i)`         |
| `p->f`         | Field `f` of the record `p` points at                     |
| `p + n`, `n + p` | Pointer `n` elements further on                         |
| `p - n`        | Pointer `n` elements back                                 |
| `p - q`        | Number of elements from `q` to `p` (a `float`)            |
| `p < q` …      | Compare two pointers of the same type                     |
| `p == null`    | Whether `p` points nowhere                                |

### Arrays turn into pointers

An array used as a value becomes a pointer to its first element: in
`float* f = a`, as an argument to a `float*` parameter, in `return`, in `*a`,
`a + n` and comparisons. Assigning to an array still **copies** it:

```c
float[3] b = a;       // copy of a
float* c = a;         // points into a
```

`&a` on an array is a pointer to the *whole array* (`float[3]*`), as in C, so
`float* f = &a;` is an error. Write `a` or `&a[0]`.

### Pointers to pointers

```c
record Matrix {
  float w;
  float h;
  float** rows;
};

void init(Matrix* m, float w, float h, float** rowSlots, float* data) {
  m->w = w;
  m->h = h;
  m->rows = rowSlots;
  for (float i = 0.0; i < h; i = i + 1.0)
    rowSlots[i] = data + i * w;        // row i starts i*w numbers in
}

float main() {
  float[64] data;
  float*[8] rows;
  Matrix m;
  init(&m, 3.0, 2.0, rows, data);
  m.rows[1][2] = 5.0;                  // row 1, column 2
  return data[5];                      // 5
}
```

### Things to know

- **There is no heap.** Pointers point at variables, fields and elements that
  already exist; the storage must be declared somewhere (usually in the caller).
- **Nothing prevents dangling pointers.** A pointer to a local is only valid
  until its function returns.
- `null` is address 0. Reading through it does not crash, it reads 0.

---

## Minecraft commands

Two built-in functions run a Minecraft command as written. Each `{}` in the
command is replaced by the next argument's value.

| Call                   | Type    | Value                                      |
|------------------------|---------|--------------------------------------------|
| `cmd("…", args…)`      | `bool`  | Whether the command succeeded              |
| `cmdValue("…", args…)` | `float` | The number the command reports             |

Both can also be used as plain statements, dropping the value.

```c
cmd("say hello");
cmd("setblock {} {} {} diamond_block", x, 64.0, z);

bool diamond = cmd("execute if block {} {} {} diamond_block", x, y, z);
float players = cmdValue("execute if entity @a");
float yaw = cmdValue("data get entity @p Rotation[0] 1000") / 1000.0;

if (cmd("execute if entity @a[x=0,y=0,z=0,dx=10,dy=20,dz=30]"))
  cmd("effect give @a[x=0,y=0,z=0,dx=10,dy=20,dz=30] night_vision 15 0 true");
```

Rules:

- The command must be a **string literal**, written out in the call.
- The number of `{}` must equal the number of arguments, and every argument
  must be a `float`.
- Values are inserted with their fraction (`37.5`, `3`). Commands that only take
  whole numbers, like block coordinates, reject a fractional value.
- Only numbers can fill a hole. Block names, selectors and states must be part
  of the literal.
- The interpreter cannot run commands. It prints them (`/say hello`) and treats
  every `cmd`/`cmdValue` value as 0, so conditions built on them only mean
  something in the datapack.

---

## Entry points

| Command                         | Runs                                            |
|---------------------------------|-------------------------------------------------|
| `/function mcl:run`             | `main`, then prints `main returned …`           |
| `/function mcl:<name> {a:1,b:2}` | Any top-level function whose parameters are all `float`/`bool`, with arguments by name |
| (every game tick)               | `void tick()`, if the program has one           |

```c
void grid(float w, float h) { … }       // /function mcl:grid {w:10,h:5}
```

- Function names in camelCase become snake_case: `sumAll` is `mcl:sum_all`.
- Every entry point **starts with fresh memory**: globals are reset to 0.
- `tick()` takes no parameters and **keeps** globals between ticks.
  Memory is set up once when the datapack loads (`/reload`).
- When the program has loops or recursion, the generated functions raise
  Minecraft's command limit first. Otherwise a long computation stops silently
  at 65,536 commands.

---

## Limitations

- Only `float` numbers, in fixed point with 3 decimals.
- No negative literals, unary minus, `%`, `++`, `+=`, `break` or `continue`.
- No underscores in names.
- Functions must be declared before they are called.
- Globals have no initialisers.
- Functions return at most one slot (no records or arrays).
- No heap allocation.
- No strings except the command in `cmd`/`cmdValue`.
- Comparing records or arrays does not work in the datapack.

---

## Grammar

Tokens (`src/lang.tok`, first match wins):

```
!WHITESPACE    = [ \t\r\n]+
!LINE_COMMENT  = //[^\n]*
!BLOCK_COMMENT = /\*[\s\S]*?\*/
NUM        = (?:[1-9][0-9]*|0)(?:\.[0-9]+)?
STRING_LIT = "[^"\n]*"
RECORD RETURN FOR WHILE IF ELSE TRUE FALSE NULL STRING FLOAT CHAR VOID BOOL
NEQ != ISEQ == LEQ <= GEQ >= AND && OR || LT < GT > NOT ! AMP &
EQ = PLUS + ARROW -> MINUS - MUL * DIV /
LPAREN ( RPAREN ) LBRACKET [ RBRACKET ] COMMA , DOT . SEMIC ;
LCURLY { RCURLY }
IDENTIFIER = [A-Za-z0-9]+
```

Grammar (`src/lang.syn`):

```
entry       : decl+ ;
decl        : recdecl | fundecl | vardecl ;
vardecl     : type IDENTIFIER (EQ expr)? SEMIC;
fundecl     : type IDENTIFIER LPAREN (param (COMMA param)*)? RPAREN compstmt;
param       : type IDENTIFIER;
recdecl     : RECORD IDENTIFIER LCURLY (vardecl)+ RCURLY SEMIC;

type        : (primtype | namedtype) MUL* (LBRACKET NUM RBRACKET)*;
primtype    : FLOAT | BOOL | VOID | STRING ;
namedtype   : IDENTIFIER;

expr        : orexpr n_orexpr? ;
n_orexpr    : OR orexpr n_orexpr? ;
orexpr      : andexpr n_andexpr? ;
n_andexpr   : AND andexpr n_andexpr? ;
andexpr     : eqexpr n_eqexpr? ;
n_eqexpr    : (ISEQ | NEQ) eqexpr n_eqexpr? ;
eqexpr      : relexpr n_relexpr? ;
n_relexpr   : (LEQ | GEQ | LT | GT) relexpr n_relexpr? ;
relexpr     : plusexpr n_plusexpr? ;
n_plusexpr  : (PLUS | MINUS) plusexpr n_plusexpr? ;
plusexpr    : mulexpr n_mulexpr? ;
n_mulexpr   : (MUL | DIV) mulexpr n_mulexpr? ;
mulexpr     : unaryexpr ;
unaryexpr   : NOT unaryexpr | MUL unaryexpr | AMP unaryexpr | accessexpr ;
accessexpr  : atom ((DOT IDENTIFIER) | (ARROW IDENTIFIER) | (LBRACKET expr RBRACKET))* ;
atom        : NUM | STRING_LIT | TRUE | FALSE | NULL | group | funcall | namedexpr;
namedexpr   : IDENTIFIER;
funcall     : IDENTIFIER LPAREN (arg (COMMA arg)*)? RPAREN;
arg         : expr;
group       : LPAREN expr RPAREN ;

stmt        : compstmt | ifstmt | whilestmt | forstmt | vardeclstmt | exprstmt | assignstmt | returnstmt;
ifstmt      : IF LPAREN expr RPAREN stmt (ELSE stmt)? ;
whilestmt   : WHILE LPAREN expr RPAREN stmt ;
forstmt     : FOR LPAREN forinit expr SEMIC forstep RPAREN stmt ;
forinit     : vardecl | assignstmt | SEMIC ;
forstep     : expr EQ expr ;
vardeclstmt : vardecl;
returnstmt  : RETURN expr? SEMIC;
assignstmt  : expr EQ expr SEMIC;
exprstmt    : expr SEMIC;
compstmt    : LCURLY stmt* RCURLY;
```
