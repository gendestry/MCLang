//
// Created by bobi on 15. 9. 26.
//
//  Runs linearized intermediate code, so a program can be checked before there
//  is an assembly backend. It keeps the same frame layout Memory planned:
//
//      incoming args (static link at FP+0, first parameter at FP+8)
//      -------- FP --------                  (the caller's SP)
//      saved old FP at FP-8, then the return-address slot
//      locals, growing downwards
//      outgoing args, written at SP upwards
//      -------- SP --------
//
//  Memory is a sparse map of bytes that reads as 0 where nothing was written.
//  The language's only number type is float, so every 8B slot holds a double;
//  addresses are doubles too, and must be whole numbers to be used. A MEM wider
//  than one slot (a record or an array) is copied byte for byte.

#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "ImcGen/data/expr/ImcExpr.h"
#include "ImcGen/data/stmt/ImcStmt.h"
#include "ImcLin/data/LinCodeChunk.h"

namespace Basic {
    class ImcLin;

    class Interpreter : public ImcExprVisitor, public ImcStmtVisitor {
    public:
        explicit Interpreter(const ImcLin &lin);

        // Calls `entry` and returns what it left in RV (0 for a void function).
        // Throws std::runtime_error when the program does something it cannot.
        double run(const std::string &entry = "main");

        // Print mode: after the run, show the final value of every global.
        void setPrint(bool on) { m_print = on; }
        // Trace mode: show every call and return, indented by call depth.
        void setTrace(bool on) { m_trace = on; }

        // ---- expressions: each leaves its value in m_value ----
        void visit(ImcCONST &e) override;
        void visit(ImcNAME &e) override;
        void visit(ImcTEMP &e) override;
        void visit(ImcMEM &e) override;
        void visit(ImcUNOP &e) override;
        void visit(ImcBINOP &e) override;
        void visit(ImcCALL &e) override;
        void visit(ImcSEXPR &e) override;

        // ---- statements: a jump leaves its target in m_jump ----
        void visit(ImcMOVE &s) override;
        void visit(ImcESTMT &s) override;
        void visit(ImcJUMP &s) override;
        void visit(ImcCJUMP &s) override;
        void visit(ImcLABEL &s) override;
        void visit(ImcSTMTS &s) override;

    private:
        using Address = std::int64_t;

        // Far apart, and both small enough for a double to hold exactly.
        static constexpr Address DATA_START = 0x10000;
        static constexpr Address STACK_TOP = 0x7FFF00000000;
        static constexpr std::size_t STEP_LIMIT = 50'000'000;

        double call(const std::string &label);
        double eval(ImcExpr &expr);
        // Where a record-sized value lives; it is only ever copied, never loaded.
        Address wideAddress(ImcExpr &expr);
        static Address toAddress(double value);

        double loadDouble(Address address) const;
        void storeDouble(Address address, double value);
        void copyBytes(Address dst, Address src, std::size_t size);
        bool sameBytes(Address a, Address b, std::size_t size) const;

        void printData() const;
        void trace(const std::string &message) const;

        const ImcLin &m_lin;
        std::unordered_map<Address, std::uint8_t> m_memory;
        std::unordered_map<std::string, Address> m_dataLabels;
        std::unordered_map<std::string, const LinCodeChunk *> m_functions; // by frame label
        std::unordered_map<std::string, std::size_t> m_jumpTargets; // label -> index in its chunk

        // ---- machine state ----
        std::unordered_map<std::size_t, double> m_temps; // the running activation's temps
        Address m_sp = STACK_TOP;
        std::size_t m_steps = 0;
        std::size_t m_depth = 0;

        double m_value = 0;
        std::optional<std::string> m_jump;

        bool m_print = false;
        bool m_trace = false;
    };
}
