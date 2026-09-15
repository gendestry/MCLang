# Optimizations

The optimizer lives in `src/ImcOpt/`, one class per pass. For Minecraft, the thing
to cut is **command count**: every command costs tick time and counts toward the
65,536-command limit.

```
ImcGen → [tree passes] → ImcLin → [linear passes] → Interpreter / McGen (+ peephole)
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
| 3 | Saving only the temps needed after a call (liveness analysis) | linear | planned |
| 4 | Copy propagation and dead code elimination | linear | planned |
| 5 | Jump threading and block merging | linear | planned |
| 6 | Common subexpression elimination | linear | planned |

The suggested order goes from the largest win for the least work to the smallest.

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

Finishing the job there would also need constant propagation. Offset merging gets
stuck the same way. Once propagation exists (pass 4), the folder can run again
after ImcLin, inside the repeat-until-nothing-changes loop; the rules can be
shared.

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

## 3. Save only needed temps (liveness analysis) — planned

`add` currently saves `$T6`, `$T7` and `$T8` on every call. A **liveness
analysis** over the linearized code finds which temps are still read after each
call; only those need saving. That is often none, saving about 8 commands per
temp per call. This is the first real dataflow analysis, and passes 4 and 6
build on it.

## 4. Copy propagation and dead code elimination — planned

- **Copy propagation:** after `MOVE(T6, ADD(FP, 8))`, use the value directly where
  `T6` is only read, instead of going through the temp.
- **Dead code elimination:** remove moves to temps that are never read, plus code
  after an unconditional jump that no label reaches (for example, what
  `ConstantFolder` leaves after turning a CJUMP into a JUMP).

These feed each other: folding creates copies to propagate, and propagating leaves
dead temps. Run all passes in a loop until none changes anything.

## 5. Jump threading and block merging — planned

- A JUMP to a label whose only statement is another JUMP goes straight to the
  final target.
- Delete labels nobody jumps to, and merge blocks that are only entered by falling
  through.

Fewer `.mcfunction` calls.

## 6. Common subexpression elimination — planned

Computing `FP+8` twice in one function: compute it once and reuse it. Saves memory
loads, but it is the most work for the least gain here, so it comes last.
