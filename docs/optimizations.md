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
| 2 | Peephole rules in McGen | inside McGen | planned |
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

## 2. Peephole rules in McGen — planned

Local rewrites while emitting commands:

- Put constants straight into `scoreboard players add` / `remove` instead of
  loading them into a scratch score first. Today `$e1 = $FP + 8000` takes
  3 commands (`set $e0 8000`, `$e1 = $FP`, `$e1 += $e0`) where 2 would do.
- Don't make scratch copies of temps that are only read.

Probably the biggest single win for the least work.

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
