//
// Created by bobi on 15. 9. 26.
//
//  Linearization of intermediate code -- the pass after ImcGen. It turns the
//  program into two flat lists the assembly emitter can walk in order:
//
//    - a data chunk per global variable: a label and how many bytes it needs
//    - a code chunk per function: the frame, the entry and exit labels, and the
//      body as a list of statements with no nesting left in them
//
//  Every CJUMP in a code chunk is followed directly by its false label, so the
//  emitter can always fall through when the condition fails.

#pragma once
#include <string>
#include <vector>

#include "LangAst.h"
#include "Mem.h"
#include "ImcGen/data/stmt/ImcStmt.h"
#include "ImcLin/data/LinCodeChunk.h"
#include "ImcLin/data/LinDataChunk.h"

namespace Basic {
    class ImcGen;
    class Memory;

    class ImcLin {
    public:
        // The whole phase: data for every global and string literal, then a code
        // chunk per function. Takes the function bodies over from `imcGen`.
        void compute(const Program &program, const Memory &memory, ImcGen &imcGen);

        // One data chunk per global variable, in declaration order.
        void collectData(const Program &program, const Memory &memory);

        // Flattens one function body into a code chunk. `entry` and `exit` are the
        // labels ImcGen used -- a `return` already jumps to `exit`.
        void addCode(const MemFrame &frame, ImcStmtPtr body, ImcLabel entry, ImcLabel exit);

        const std::vector<LinDataChunk> &dataChunks() const { return m_data; }
        const std::vector<LinCodeChunk> &codeChunks() const { return m_code; }

        // Print mode: emit each chunk as it is added.
        void setPrint(bool on) { m_print = on; }
        bool printing() const { return m_print; }

    private:
        // CJUMP(c, pos, neg) -> CJUMP(c, pos, fall), LABEL fall, JUMP neg
        static std::vector<ImcStmtPtr> linearize(std::vector<ImcStmtPtr> stmts);

        void printCode(const LinCodeChunk &chunk) const;
        void print(const std::string &message) const;

        std::vector<LinDataChunk> m_data;
        std::vector<LinCodeChunk> m_code;
        bool m_print = false;
    };
}
