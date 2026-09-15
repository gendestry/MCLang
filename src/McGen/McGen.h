//
// Created by bobi on 15. 9. 26.
//
//  Datapack generation -- turns linearized code into Minecraft Java Edition
//  functions (1.21+: it needs function macros and `return run`).
//
//  How the machine maps onto commands:
//
//    - numbers: scoreboards only hold 32-bit ints, so every float is fixed point,
//      stored multiplied by SCALE (3.5 is 3500). Addresses are numbers too, so
//      they are scaled the same way.
//    - temps: fake players on the `mcl` objective ($T5, $FP, $SP, $RV); `$e0`,
//      `$e1`, ... are scratch holders for the statement being run.
//    - memory: one sparse compound, `storage mcl:mem ram`, keyed by slot number
//      (address / 8). Two macro functions read and write it; a slot never
//      written reads as 0, the same as the interpreter.
//    - control flow: every label starts its own function (a block). A jump is
//      `return run function <block>`, and a jump to the exit label is `return 1`,
//      which unwinds the whole chain back to the function's own file.
//    - calls: the function's own file is the prologue, a call into its entry
//      block, then the epilogue. Scoreboards are global, so the prologue saves
//      the caller's FP and every temp this function writes onto a stack
//      (`storage mcl:mem saved`), and the epilogue puts them back.
//
//  Output: <dir>/pack.mcmeta, the load tag, and data/mcl/function/ with `load`,
//  `run` (call main and print what it returned), `rt/` (memory access) and
//  `fn/<function>/` (the generated code).

#pragma once
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/stmt/ImcStmt.h"
#include "ImcLin/data/LinCodeChunk.h"

namespace Basic {
    class ImcLin;

    class McGen : public ImcExprVisitor, public ImcStmtVisitor {
    public:
        explicit McGen(const ImcLin &lin) : m_lin(lin) {}

        // Generates the datapack and writes it under `outDir`, replacing any
        // previously generated code there. Returns false (and writes nothing)
        // when the program uses something the datapack cannot express yet.
        bool generate(const std::string &outDir);

        const std::vector<std::string> &errors() const { return m_errors; }

        // Print mode: list every file written, with its command count.
        void setPrint(bool on) { m_print = on; }
        bool printing() const { return m_print; }

        // Optimize: run the peephole rules (McPeephole) over every generated block,
        // and save only the temps still live across a call (see genChunk).
        void setOptimize(bool on) { m_optimize = on; }

        // ---- expressions: each leaves the holder with its value in m_holder ----
        void visit(ImcCONST &e) override;
        void visit(ImcNAME &e) override;
        void visit(ImcTEMP &e) override;
        void visit(ImcMEM &e) override;
        void visit(ImcUNOP &e) override;
        void visit(ImcBINOP &e) override;
        void visit(ImcCALL &e) override;
        void visit(ImcSEXPR &e) override;

        // ---- statements: each appends commands to the current block ----
        void visit(ImcMOVE &s) override;
        void visit(ImcESTMT &s) override;
        void visit(ImcJUMP &s) override;
        void visit(ImcCJUMP &s) override;
        void visit(ImcLABEL &s) override;
        void visit(ImcSTMTS &s) override;

    private:
        static constexpr long long SCALE = 1000;
        static constexpr long long DATA_START = 64;      // bytes
        static constexpr long long STACK_TOP = 800000;   // bytes; x SCALE still fits in an int

        struct Block {
            std::string label;
            std::vector<std::string> lines;
        };

        // ---- per function ----
        void genChunk(const LinCodeChunk &chunk);
        // Every temp a call to each function can overwrite: its own writes and
        // those of every function it reaches through calls.
        void computeClobbers();
        // The temps to save around statement `stmt` when it makes a call.
        std::vector<std::size_t> callSaves(const ImcStmt &stmt, const std::set<std::size_t> &liveOut) const;

        // ---- naming ----
        static std::string functionName(const std::string &function); // mcl:fn/<name>
        std::string blockName(const std::string &label) const;       // mcl:fn/<name>/<label>
        std::string jumpTo(const std::string &label) const;           // `return ...` for a jump

        // ---- commands ----
        std::string gen(ImcExpr &expr);
        std::string scratch();
        void emit(std::string line) { m_blocks.back().lines.push_back(std::move(line)); }
        long long fixed(double value);

        static std::string setLine(const std::string &holder, long long value);
        static std::string opLine(const std::string &dst, const char *op, const std::string &src);
        static std::string addLine(const std::string &holder, long long amount);

        void copy(const std::string &dst, const std::string &src);
        void load(const std::string &address, const std::string &dst);
        void store(const std::string &address, const std::string &value);
        void copySlots(const std::string &dst, const std::string &src, std::size_t size);
        void emitCall(ImcCALL &call);

        // ---- output ----
        void addRuntime();
        void writeFile(const std::string &path, const std::vector<std::string> &lines) const;

        void error(std::string message) { m_errors.push_back(std::move(message)); }

        const ImcLin &m_lin;
        std::unordered_map<std::string, long long> m_data; // data label -> address in bytes
        std::map<std::string, std::vector<std::string>> m_files; // path under function/ -> commands

        // ---- state while a chunk is generated ----
        std::string m_function;
        std::string m_exit;
        std::vector<Block> m_blocks;
        bool m_terminated = true; // the current block already ends in a jump
        std::size_t m_scratch = 0;
        std::string m_holder;

        std::vector<std::string> m_errors;
        bool m_print = false;
        bool m_optimize = false;
        std::size_t m_peephole = 0; // rewrites made, for print mode
        std::size_t m_saves = 0;    // temps saved at call sites, for print mode
        std::unordered_map<std::string, std::set<std::size_t>> m_clobbers; // function -> temp ids
    };
}
