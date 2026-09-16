# Optimizations

The optimizer lives in `src/ImcOpt/`, one class per pass. For Minecraft, the thing
to cut is **command count**: every command costs tick time and counts toward the
65,536-command limit.

```
ImcGen → Promoter → ConstantFolder → Inliner → ConstantFolder          (tree passes, step 7b)
       → ImcLin
       → LinOptimizer: fold, CSE, copy propagation, dead code,
                       loop-invariant motion, jump threading — until nothing changes (step 8b)
       → Interpreter / McGen (liveness-based saves, strength reduction,
                              comparison jumps, cmd arguments, peephole)
```

- **Tree passes** run on ImcGen's statement trees, where an expression like
  `pts[1]` is still a single expression.
- **Linear passes** run on ImcLin's flat statement lists, where labels and jumps
  are explicit, so a control-flow graph can be built.

The optimizer is controlled by `bool optimize` in `src/main.cpp`.

**Testing:** every program must return the same value with `optimize = true` as
with `optimize = false`. The interpreter and the Minecraft simulator already give
the "before" result, so each pass can be checked against them. Also compare the
command count of the generated datapack.

## Status

| # | Optimization | Where | Status |
|---|--------------|-------|--------|
| 1 | Constant folding and algebraic simplification | tree, before ImcLin | **done** (`ConstantFolder`) |
| 2 | Peephole rules in McGen | inside McGen | **done** (`McPeephole`) |
| 3 | Saving only the temps needed after a call (liveness analysis) | linear, used by McGen | **done** (`Liveness`) |
| 4 | Copy propagation and dead code elimination | linear | **done** (`CopyPropagation`, `DeadCode`) |
| 5 | Jump threading and block merging | linear | **done** (`JumpThreading`) |
| 6 | Common subexpression elimination | linear | **done** (`CommonSubexpr`) |
| 7 | Register promotion: plain variables in temps | tree, before folding | **done** (`Promoter`) |
| 8 | Inlining small leaf functions | tree, after promotion | **done** (`Inliner`) |
| 9 | Loop-invariant code motion | linear | **done** (`LoopInvariant`) |
| 10 | McGen code generation: strength reduction, comparison jumps, `cmd` arguments | inside McGen | **done** |

All linear passes are in `src/ImcOpt/LinearPasses.{h,cpp}`, one class each, driven
by `LinOptimizer`. Shared helpers: `ImcWalk.h` (looking into and rewriting
expressions, control-flow facts) and `ImcClone.{h,cpp}` (deep copies, with renaming
for the inliner).

## Results, everything on

Every program returns the same value in the interpreter and in the simulated
datapack with `optimize` on and off, and runs the same raw commands (`setblock`
positions compared line by line).

| Program | Commands, off → on | Executed, off → on |
|---------|--------------------|--------------------|
| records, `&` parameter, calls | 391 → 209 | 496 → 230 |
| constants / `if` | 237 → 51 | 274 → 36 |
| loop + recursion (`fact`) | 492 → 180 | 2442 → 707 |
| `fib` + helper in a loop | 517 → 208 | 16159 → 6037 |
| nested loops, records, inlining, CSE, `cmd` | 1469 → 386 | 11625 → 1642 |
| `grid {w:4,h:3}` | 221 → 52 | 1603 → 229 |

`grid` costs 7 commands per block placed, down from about 115.

---

## 1. Constant folding (`ConstantFolder`) — done

Files: `src/ImcOpt/ConstantFolder.h`, `src/ImcOpt/ConstantFolder.cpp`, step 7b in
`src/main.cpp`.

On `src/input.txt` it cuts the datapack from 374 to 326 commands, and `main`
returns the same value (7.5) with the optimizer on or off.

### What it does

Children are folded before their parents, so every rule sees operands that are
already folded.

- **Constant operations are evaluated at compile time:** `MUL(1, 32)` → `32`. This
  covers arithmetic, comparisons, AND/OR, NEG and NOT, with the same semantics as
  the interpreter. Division or modulo by a constant 0 is left alone, so it still
  fails at runtime.
- **Identities are removed:** `x+0`, `x-0`, `x*1` and `x/1` → `x`. `x*0` → `0`, but
  only when `x` contains no call or SEXPR, so no side effect is lost.
- **Constant offsets merge:** `(x + 8) - 3 + 1` → `x + 6`.
- **A CJUMP on a constant becomes a JUMP:** `if (1.0 < 2.0)` no longer tests
  anything at runtime.

Numbers are doubles here, as in the interpreter. McGen's fixed-point arithmetic
may round a runtime division slightly differently from the folded result
(`1/3*3` folds to 1 but computes to 0.999), so folding can only make results more
precise.

### Why it runs before ImcLin

ImcLin moves every nested operand into a temp. On the tree, `pts[1]` folds
completely:

```
ADD(FP, MUL(CONST(1), CONST(32)))   ->   ADD(FP, CONST(32))
```

After ImcLin the same code is split across two statements:

```
MOVE(T5, MUL(CONST(1), CONST(32)))  ->  MOVE(T5, CONST(32))
MOVE(T6, ADD(TEMP(FP), TEMP(T5)))   // stuck: the folder can't see that T5 is 32
```

Finishing the job there also needs constant propagation. That now exists (pass 4),
so `LinOptimizer` runs the same folder over every linear statement in its loop:
once `T5` is propagated as 32, `ADD(TEMP(FP), CONST(32))` is there to fold.

### Code walkthrough

#### `fold(slot)`: replacing nodes

Every node is owned by a `unique_ptr` in its parent. A visitor receives
`ImcBINOP &e`, not the pointer that owns it, so it can't replace itself. Instead:

```cpp
void ConstantFolder::fold(ImcExprPtr &slot) {
    m_expr = nullptr;
    slot->accept(*this);            // the visit may set m_expr to a replacement
    if (m_expr)
        slot = std::move(m_expr);   // swap it into the parent's pointer
}
```

- The parent calls `fold(e.fst)` on its own child pointer.
- If the visit sets `m_expr`, `fold` swaps the replacement in. The old node is
  destroyed there, after its visit has returned.
- If the visit leaves `m_expr` null, the node stays.

`fold(ImcStmtPtr &)` does the same for statements, using `m_stmt`.

`replace(with)` stores the replacement and increments `m_rewrites`. Every rewrite
goes through it, so the count is always accurate.

#### `run()`: the driver

```cpp
do {
    before = m_rewrites;
    fold(body);
} while (m_rewrites != before);
```

It folds the whole body and repeats if anything changed. Because children are
folded first, one pass is almost always enough; the loop is a safety net. In
print mode it then prints `'main' folded N node(s)`.

#### Helpers

- **`evaluate(oper, a, b)`** computes an operator at compile time, matching
  `Interpreter::visit(ImcBINOP&)`: comparisons give 1 or 0, MOD uses `fmod`.
  Division or modulo by 0 returns `nullopt`, so the node is kept.
- **`offset(x, c)`** builds `x + c`, or `x - |c|` when `c` is negative.
- **`asConst(expr)`** returns the `ImcCONST*`, or null.
- **`hasCall(expr)`** checks the subtree for a `CALL` or `SEXPR`. Used by `x * 0`,
  where dropping `x` would also drop the call's side effects.

#### Expression visits

- **Leaves** (`CONST`, `NAME`, `TEMP`): nothing to do.
- **`MEM`, `CALL`, `SEXPR`**: fold their children only. A memory read or a call is
  never a constant.
- **`UNOP`**: folds the operand; if it is a constant, the node becomes `-c` or `!c`.
- **`BINOP`**, in order:
  1. Fold both children.
  2. Both operands constant: replace the node with `evaluate(...)` and stop.
  3. For `ADD` and `MUL`, swap a constant on the left to the right, so the rules
     below only check `snd`.
  4. Right operand not a constant: stop.
  5. Rules by operator:
     - **`ADD`/`SUB`**: the constant becomes a signed `delta` (`-c` for SUB).
       - `delta == 0`: the node becomes `fst`.
       - `fst` is itself `ADD`/`SUB` with a constant: merge them into
         `inner.fst ± total`. A total of 0 leaves just `inner.fst`.
     - **`MUL`**: `x*1` → `x`; `x*0` → `0` if `x` has no call.
     - **`DIV`**: `x/1` → `x`.

`replace(std::move(e.fst))` moves the child out first. `e` itself is destroyed
only after the visit returns, so nothing points to freed memory.

#### Statement visits

- **`MOVE`, `ESTMT`, `STMTS`**: fold their children.
- **`JUMP`, `LABEL`**: nothing to do.
- **`CJUMP`**: folds the condition; if it becomes a constant, the node becomes
  `JUMP(pos)` or `JUMP(neg)`. The now-unreachable code is left in place for a
  later pass to remove.

#### Hook-up in `main.cpp` (step 7b)

```cpp
if (optimize) {
    Basic::ConstantFolder folder;
    folder.setPrint(printNames);
    for (auto &f : imcGen.functions())
        rewrites += folder.run(f.frame->label, f.body);
}
```

It runs before `imcLin.compute(...)` takes the bodies over, rewriting every
function body in place.

#### Adding a rule

Add a `case` in `visit(ImcBINOP&)` after step 4, and apply it with `replace(...)`.

- Only drop an operand when `hasCall` is false for it.
- A rule must not keep matching its own output, or `run()` never finishes. This is
  why a SUB is only rewritten when two constants actually merge.

---

## 2. Peephole rules in McGen (`McPeephole`) — done

Files: `src/McGen/McPeephole.h`, `src/McGen/McPeephole.cpp`. McGen runs it over
every block once the block is generated, when `McGen::setOptimize(true)` is set
(`main.cpp` passes `optimize`). The prologue and epilogue file isn't touched.

It works on the command text, not the IMC. McGen puts every value in a scratch
holder (`$e0`, `$e1`, ...) first, so many commands only shuffle values between
holders; the pass removes them.

### Results

Checked with a small simulator for the emitted command subset: every program
returns the same value with `optimize` on and off (the constant folder and the
peephole pass together).

| Program | Commands, off | Commands, on | Executed, off | Executed, on |
|---------|---------------|--------------|---------------|--------------|
| `src/input.txt` (records, calls) | 374 | 273 | 496 | 383 |
| constants / `if` | 220 | 113 | 274 | 150 |
| loop, recursion, `*` `/`, comparisons | 456 | 377 | 2442 | 2013 |

Command counts are for the whole datapack, including the runtime files. Loading a
value from memory is what makes up most of the rest.

### Before and after

```mcfunction
# MOVE(TEMP(T2), MEM8(ADD(TEMP(FP), CONST(8))))       -- before: 9 commands
scoreboard players set $e0 mcl 8000
scoreboard players operation $e1 mcl = $FP mcl
scoreboard players operation $e1 mcl += $e0 mcl
scoreboard players operation $slot mcl = $e1 mcl
scoreboard players operation $slot mcl /= #slot mcl
execute store result storage mcl:args slot int 1 run scoreboard players get $slot mcl
function mcl:rt/load with storage mcl:args
scoreboard players operation $e2 mcl = $mem mcl
scoreboard players operation $T2 mcl = $e2 mcl

# after: 6 commands
scoreboard players operation $slot mcl = $FP mcl
scoreboard players add $slot mcl 8000
scoreboard players operation $slot mcl /= #slot mcl
execute store result storage mcl:args slot int 1 run scoreboard players get $slot mcl
function mcl:rt/load with storage mcl:args
scoreboard players operation $T2 mcl = $mem mcl
```

### Rules

Applied until none matches. After each rewrite the scan starts again from the top
of the block.

1. **Constant into add / remove** (`foldConstant`): `set X c`, then the one line
   that reads X, `D += X` or `D -= X` → `add D c` / `remove D c`. A negative amount
   flips `add` and `remove`, since `add` only takes a non-negative number.
2. **Constant comparison** (also `foldConstant`): `set X c`, then
   `execute if|unless score A OP X run ...` → `execute if|unless score A matches R run ...`,
   where `<` gives `..c-1`, `<=` gives `..c`, `=` gives `c`, `>=` gives `c..` and
   `>` gives `c+1..`.
3. **Compute in place** (`coalesce`): at `Y = X`, where X is a scratch that's dead
   afterwards, walk back to the line that defines X, rename X to Y in every line
   from there on, and delete the copy.
   `$e0 = $T2; $e0 += $T3; $RV = $e0` → `$RV = $T2; $RV += $T3`.
4. **No-ops** (`noOp`): `add X 0`, `remove X 0` and `X = X` are deleted. Rule 3
   often leaves these behind.

### Why it's safe

- **Only scratch holders (`$eN`) are removed or renamed away.** McGen defines one
  afresh in every statement that uses it (`m_scratch` resets per statement), so
  a scratch is **dead** once its next mention in the block is a line that sets it
  without reading it (`defines`), or the block ends. Other blocks never read it.
- **A holder is recognized as a `<name> mcl` token pair,** so comments and JSON
  text never match.
- **No rewrite reaches across a `function` line.** A called MCLang function
  clobbers every scratch, and `rt/load` / `rt/store` read `$slot` and `$mem`
  without naming them.
- **Rule 3 moves a write to Y earlier,** so it gives up when:
  - Y is mentioned anywhere between X's definition and the copy, since those
    lines would see the new value
  - there's a `return` in between, since the block being returned into could read
    Y early
- **Amounts and ranges that would leave the 32-bit int range are left alone.**

### Adding a rule

Write a `bool rule(std::vector<std::string> &lines, std::size_t i)` that rewrites
around line `i` and returns whether it did, and call it from the loop in
`McPeephole::run`. Use `split`/`join`, `mentions`, `defines` and `dead`, and follow
the safety rules above.

## 3. Save only needed temps (`Liveness`) — done

Files: `src/ImcOpt/Liveness.h`, `src/ImcOpt/Liveness.cpp` (the analysis), and
`McGen::computeClobbers`, `McGen::callSaves` and `McGen::genChunk` (how it's used).
It's switched by the same `McGen::setOptimize`.

### The problem

Scoreboards are global, so a call can overwrite any temp its caller still needs.
Unoptimized, every function's **prologue** saves the caller's FP **and every temp
the function writes**, and the epilogue restores them all: 4 commands per temp,
on every call, whether or not the caller needs them. `add` saved three temps on
every call even though `main` read none of them afterwards.

### The fix: caller-saves, driven by liveness

The **caller** saves a temp around a call only when both are true:

1. **It is live after the call:** it may still be read later. That's what the
   liveness analysis gives.
2. **The callee can overwrite it.** Temp ids are unique across the whole program
   (`ImcTemp::fresh`), so a callee can only write its own temps and those of the
   functions it reaches. `computeClobbers` builds that set for every function as
   a fixpoint over the call graph, which also handles recursion. A call to a
   function it doesn't know is treated as overwriting everything.

`save = liveOut(call) ∩ clobbers(callee) − {the call's result temp}`

The result temp is excluded because the statement writes it after the call, and
restoring it would undo that. The prologue now only saves FP.

A non-recursive call usually saves nothing. In `fib`, only the first call's result
is saved, around the second call:

```mcfunction
# MOVE(TEMP(T10), CALL(fib, @0:TEMP(T7), @8:TEMP(T9)))
execute store result storage mcl:mem tmp int 1 run scoreboard players get $T6 mcl
data modify storage mcl:mem saved append from storage mcl:mem tmp
...                                   # arguments, function mcl:fn/fib
scoreboard players operation $T10 mcl = $RV mcl
execute store result score $T6 mcl run data get storage mcl:mem saved[-1]
data remove storage mcl:mem saved[-1]
# MOVE(TEMP(RV), ADD(TEMP(T6), TEMP(T10)))
```

### The liveness analysis

The classic backward dataflow over one `LinCodeChunk`:

```
out[s] = ∪ in[t]   for every successor t of s
in[s]  = uses[s] ∪ (out[s] − defs[s])
```

- **Successors:** a `JUMP` goes to its label's index, a `CJUMP` to both labels,
  and anything else to the next statement. A jump to the exit label, or off the
  end, has none.
- **`uses`:** every temp in the statement's expressions. A `MOVE` into a `TEMP`
  doesn't read it, but a `MOVE` into a `MEM` reads its address.
- **`defs`:** the temp a `MOVE` writes.
- **Iteration:** it sweeps backwards, since facts flow that way, and repeats until
  no set changes.
- **FP and RV aren't tracked.** FP is saved by the prologue, and RV is copied out
  right after every call.

`Liveness::callIn` finds the call a statement makes. In linearized code that's
only `MOVE(TEMP, CALL)` or `ESTMT(CALL)`.

Passes 4 (dead code elimination) and 6 can reuse `Liveness`.

### Results

All three passes together, checked in the simulator. Every program returns the
same value with `optimize` on and off.

| Program | Commands, off → on | Executed, off → on | Saves at calls |
|---------|--------------------|--------------------|----------------|
| `src/input.txt` | 374 → 217 | 496 → 299 | 0 |
| constants / `if` | 220 → 81 | 274 → 102 | 0 |
| loop, recursion (`fact`) | 456 → 261 | 2442 → 1713 | 1 |
| `fib` + helper in a loop | 462 → 274 | 16159 → 8914 | 1 |

The recursive `fib` shows the biggest runtime win: it executes 45% fewer commands.

## The linear passes (4, 5, 6, 9)

`LinOptimizer::run` (step 8b in `main.cpp`) repeats, on each code chunk, until a
whole round changes nothing (with a cap of 32 rounds as a safety net):

1. `ConstantFolder` on each statement
2. `CommonSubexpr`
3. `CopyPropagation`
4. `DeadCode`
5. `LoopInvariant`
6. `JumpThreading`

They feed each other. CSE turns a computation into a copy, copy propagation makes
the copy unused, dead code removes it; a propagated constant lets the folder turn
a CJUMP into a JUMP, which leaves unreachable code and a label nobody jumps to.

Two facts make these passes simple:

- **Only a MOVE changes a temp.** In intermediate code, temps belong to one call; a
  call can't touch its caller's temps. (In the datapack scoreboards are global, but
  McGen saves whatever a call could clobber — pass 3.)
- **Blocks start at labels.** A fact that holds within a block is dropped at every
  label, because another block may jump there. Only liveness and loop-invariant
  motion look across blocks.

### 4a. Copy propagation (`CopyPropagation`)

After `MOVE(a, b)`, `MOVE(a, CONST)` or `MOVE(a, NAME)`, later reads of `a` in the
same block read the value directly, until `a` or `b` is written again. RV is never
propagated, since every call changes it.

### 4b. Dead code elimination (`DeadCode`)

- Statements after a JUMP or CJUMP, before the next label: never reached.
- `MOVE(t, E)` where `t` isn't live afterwards (from `Liveness`): removed, or turned
  into `ESTMT(call)` when E is a call, so the call still runs.
- `ESTMT(E)` with no call in E: removed.

It repeats until nothing changes, since removing one move can make the moves that
fed it dead.

### 5. Jump threading and block merging (`JumpThreading`)

In the datapack every label is a `.mcfunction` and every jump a function call, so
removed labels and jumps are fewer files and fewer calls.

- **Threading:** a jump to a label whose block is only `JUMP M` goes to `M`
  directly (chains are followed; a loop of empty jumps stops inside the loop).
  This removes the `LABEL fall; JUMP neg` that ImcLin puts after every CJUMP.
- **CJUMP simplification:** `CJUMP(CONST, a, b)` and `CJUMP(x, a, a)` become JUMPs.
- **Fall-through jumps:** `JUMP L` directly followed by `LABEL L` is dropped.
- **Unused labels:** a label nobody jumps to is dropped. If the block before falls
  into it, the two blocks merge; if not, the block is unreachable and goes too.
  The entry label always stays: it's where the prologue calls in.

### 6. Common subexpression elimination (`CommonSubexpr`)

Within a block, `MOVE(a, E)` becomes `MOVE(a, b)` when an earlier `MOVE(b, E)` is
still valid. Expressions are compared by their printed form. An entry is dropped
when a temp it reads, or `b`, is written; an entry that reads memory is also
dropped at anything that may write memory: a store, a call, or a `cmd` (a raw
command can run `data modify`).

In the test program `(i + j) * 3.0 + (i + j) * 3.0` becomes one `MUL` and `T41 + T41`.

### 9. Loop-invariant code motion (`LoopInvariant`)

A loop is what ImcGen makes of `while` and `for`: `LABEL top`, a body, and a
`JUMP top` further down. `MOVE(t, E)` moves in front of the loop when all of these
hold:

- **The loop is closed:** nothing outside jumps to a label inside it, and it's
  entered by falling into `top`, so "in front" is a place every entry passes.
- **E is invariant:** no temp it reads is written in the loop; if it reads memory,
  nothing in the loop may write memory.
- **E can't fail or act:** no call, and no DIV or MOD (a division by zero that
  would never have run must not run now).
- **t is written only there,** and **t isn't live at `top`**. The second covers a
  loop that runs zero times: no path from `top` reads the value `t` had before.

It hoists one statement at a time and starts over, so a hoisted statement can
leave an inner loop and then the outer one.

## 7. Register promotion (`Promoter`)

The biggest single win. Every local and parameter used to live in
`storage mcl:mem`, so each read cost 6 commands and a macro call, and each write
the same. Now a variable that is only read and written gets a temp: one scoreboard
entry.

Runs first on the trees, when a variable is still spelled exactly as ImcGen wrote
it, `MEM8(ADD(TEMP(FP), CONST(offset)))`. A frame slot is promoted when **every**
mention of `ADD(TEMP(FP), CONST(offset))` is directly inside a one-slot MEM.
Anything else keeps it in memory:

- `&x` passes `ADD(FP, offset)` itself
- a record or array is read as a wider MEM, or through `ADD(ADD(FP, offset), ...)`

(Promoting a `&T` parameter is fine: the slot holds the address, and that address
is what the temp holds; `*v` stays a MEM.)

Every `MEM8(ADD(FP, offset))` becomes `TEMP(t)`, and the body gets, in front:

- `MOVE(t, MEM8(ADD(FP, offset)))` for a parameter (offset > 0): loaded once
- `MOVE(t, CONST(0))` for a local: a temp read before it's written would be an
  error in the interpreter. Dead code elimination removes it when the variable is
  always assigned first, which is almost always.

Safe because a local is only reachable from its own function: the language has no
nested functions (which would reach outer locals through the static link), and
temps are saved across recursive calls by pass 3. `ExprCanonizer` still doesn't
park temps before a call in the same expression; that stays correct because a call
can't change its caller's temps.

## 8. Inlining (`Inliner`)

A call costs the argument stores, the prologue and the epilogue — around 15
commands before the body starts — so a small function is cheaper copied in.
Runs after promotion and folding, then the folder runs again.

A function is inlined when it:

- **is a leaf:** calls nothing, so it isn't recursive
- **has no memory left:** every parameter was promoted, and nothing but
  Promoter's loads mentions FP
- **is small:** at most `Inliner::MAX_NODES` (80) nodes
- **takes only one-slot arguments** at that call

`CALL(f, args)` becomes

```
SEXPR(STMTS(MOVE(result, 0),
            MOVE(a1, arg1), ...,            -- the arguments, in order
            <body, copied with fresh temps and labels,
             parameter p -> its argument temp, RV -> result, exit -> end>,
            LABEL end),
      TEMP(result))
```

`clone` with an `ImcRenaming` does the copying: a temp or label seen for the first
time gets a fresh one, and the pre-seeded entries (parameters, RV, the exit label)
win. The function itself stays: entry points and non-inlined callers use it.

`clamp(v, lo, hi)` and `bump(&v, by)` in the test program are inlined; `dist2`
takes records, so it isn't.

## 10. McGen code generation

All switched by `McGen::setOptimize`.

- **Strength reduction** (`multiplyByWhole`): `x * k` and `x / k` for a whole
  constant `k`. The general fixed-point multiply splits the product so it can't
  overflow (7 commands); with `k` unscaled, `x * k` is exact in 3 commands
  (`h = x; set c k; h *= c`), `x * 2` is `h = x; h += x`, and `x / k` is
  `floor(x / k)` like the general code.
- **Comparison jumps:** `CJUMP(LTH(a, b), L1, L2)` jumps on the comparison instead
  of first computing 0 or 1000 and testing that — 2 commands instead of 4:

  ```mcfunction
  execute if score $T2 mcl < $T4 mcl run return run function mcl:fn/grid/l6
  return run function mcl:fn/grid/l7
  ```

  With a constant operand the peephole pass turns it into `matches`; its constant
  rule now also looks at a line that runs a function *after* reading the constant.
- **`cmd` arguments:** a temp goes into `mcl:args` in one command,
  `execute store result storage mcl:args a0 int 0.001 run scoreboard players get $T2 mcl`,
  instead of copy, `/= #scale`, store. A constant is written as
  `data modify storage mcl:args a0 set value 3`. `int 0.001` rounds toward zero,
  where `/= #scale` rounds down: the results differ only for a negative number that
  isn't whole (`-0.5` gives 0 instead of -1). Whole numbers are exact (checked for
  every value in ±3,000,000).
- **Peephole, compute in place:** `$e1 = $T2; add $e1 1000; $T2 = $e1` becomes
  `add $T2 1000`. Rule 3 used to give up because the window mentions `$T2`; it now
  allows the one mention in the defining `X = Y` line itself.
