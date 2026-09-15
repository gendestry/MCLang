//
// Created by bobi on 15. 9. 26.
//

#include "ImcLin/Interpreter.h"

#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "ImcGen/data/expr/ImcBINOP.h"
#include "ImcGen/data/expr/ImcCALL.h"
#include "ImcGen/data/expr/ImcCONST.h"
#include "ImcGen/data/expr/ImcMEM.h"
#include "ImcGen/data/expr/ImcNAME.h"
#include "ImcGen/data/expr/ImcSEXPR.h"
#include "ImcGen/data/expr/ImcTEMP.h"
#include "ImcGen/data/expr/ImcUNOP.h"
#include "ImcGen/data/stmt/ImcCJUMP.h"
#include "ImcGen/data/stmt/ImcESTMT.h"
#include "ImcGen/data/stmt/ImcJUMP.h"
#include "ImcGen/data/stmt/ImcLABEL.h"
#include "ImcGen/data/stmt/ImcMOVE.h"
#include "ImcGen/data/stmt/ImcSTMTS.h"
#include "ImcLin/ImcLin.h"
#include "Utils/Colors/Font.h"

namespace Basic {
    namespace {
        std::string nameText(const std::string &name) {
            return Utils::Font::colorYellow + "'" + name + "'" + Utils::Font::colorReset;
        }
        std::string dimText(const std::string &text) {
            return Utils::Font::colorDim + text + Utils::Font::colorReset;
        }

        // Whole numbers print as "3" rather than "3.000000".
        std::string number(double value) {
            std::string s = std::to_string(value);
            s.erase(s.find_last_not_of('0') + 1);
            if (s.back() == '.')
                s.pop_back();
            return s;
        }
    }

    Interpreter::Interpreter(const ImcLin &lin) : m_lin(lin) {
        // Globals and strings sit back to back, each rounded up to whole slots.
        Address next = DATA_START;
        for (const LinDataChunk &data : lin.dataChunks()) {
            m_dataLabels[data.label.name] = next;
            if (data.init)
                for (std::size_t i = 0; i < data.init->size(); ++i)
                    m_memory[next + static_cast<Address>(i)] = static_cast<std::uint8_t>((*data.init)[i]);
            next += static_cast<Address>((data.size + SLOT_SIZE - 1) / SLOT_SIZE * SLOT_SIZE);
        }

        // Labels are unique across the program, so one map serves every chunk.
        for (const LinCodeChunk &code : lin.codeChunks()) {
            m_functions[code.frame->label] = &code;
            for (std::size_t i = 0; i < code.stmts.size(); ++i)
                if (const auto *label = dynamic_cast<const ImcLABEL *>(code.stmts[i].get()))
                    m_jumpTargets[label->label.name] = i;
        }
    }

    void Interpreter::trace(const std::string &message) const {
        if (!m_trace)
            return;
        std::cout << std::string((m_depth + 1) * 2, ' ') << message << std::endl;
    }

    // ---- running -------------------------------------------------------------

    double Interpreter::run(const std::string &entry) {
        m_sp = STACK_TOP;
        m_steps = 0;
        m_depth = 0;
        m_temps.clear();

        storeDouble(m_sp, 0); // the entry point's static link: there is no outer frame
        const double result = call(entry);

        if (m_print)
            printData();
        return result;
    }

    double Interpreter::call(const std::string &label) {
        auto found = m_functions.find(label);
        if (found == m_functions.end())
            throw std::runtime_error("call to '" + label + "', which has no code");
        const LinCodeChunk &chunk = *found->second;
        const MemFrame &frame = *chunk.frame;

        // ---- prologue ----
        // The caller wrote the arguments at its SP, so that SP is this frame's FP.
        std::unordered_map<std::size_t, double> callerTemps;
        std::swap(callerTemps, m_temps);
        const auto callerFp = callerTemps.find(ImcTemp::FP().id);
        const Address fp = m_sp;
        storeDouble(fp - static_cast<Address>(SLOT_SIZE),
                    callerFp == callerTemps.end() ? 0 : callerFp->second); // saved old FP
        m_temps[ImcTemp::FP().id] = static_cast<double>(fp);
        m_sp -= static_cast<Address>(frame.size());

        trace(dimText("call ") + nameText(label) + dimText(" FP=" + std::to_string(fp)));
        ++m_depth;

        // ---- body ----
        std::size_t pc = m_jumpTargets.at(chunk.entry.name);
        while (true) {
            if (++m_steps > STEP_LIMIT)
                throw std::runtime_error("stopped after " + std::to_string(STEP_LIMIT)
                                         + " statements -- is there an endless loop?");
            if (pc >= chunk.stmts.size())
                throw std::runtime_error("ran off the end of '" + label + "'");

            m_jump.reset();
            chunk.stmts[pc]->accept(*this);
            if (!m_jump) {
                ++pc;
                continue;
            }
            if (*m_jump == chunk.exit.name)
                break;
            auto target = m_jumpTargets.find(*m_jump);
            if (target == m_jumpTargets.end())
                throw std::runtime_error("jump to unknown label " + *m_jump);
            pc = target->second;
        }

        // ---- epilogue ----
        const auto rv = m_temps.find(ImcTemp::RV().id);
        const double result = rv == m_temps.end() ? 0 : rv->second;
        m_sp += static_cast<Address>(frame.size());
        std::swap(callerTemps, m_temps);
        // The call ran inside one of the caller's statements; the jump to this
        // function's exit label must not leak out as that statement's jump.
        m_jump.reset();

        --m_depth;
        trace(dimText("return ") + nameText(label) + dimText(" -> ") + number(result));
        return result;
    }

    double Interpreter::eval(ImcExpr &expr) {
        expr.accept(*this);
        return m_value;
    }

    Interpreter::Address Interpreter::wideAddress(ImcExpr &expr) {
        auto *mem = dynamic_cast<ImcMEM *>(&expr);
        if (!mem || mem->size <= SLOT_SIZE)
            throw std::runtime_error("expected a record-sized value, got " + expr.toString());
        return toAddress(eval(*mem->addr));
    }

    // Every slot the program can reach starts on a multiple of 8: the stack top,
    // every frame, every global and every field is a whole number of slots. An
    // address anywhere else came from an array index with a fraction in it.
    Interpreter::Address Interpreter::toAddress(double value) {
        if (!std::isfinite(value) || value != std::floor(value)
            || static_cast<Address>(value) % static_cast<Address>(SLOT_SIZE) != 0)
            throw std::runtime_error("address " + number(value)
                                     + " is not on a slot boundary (an array index with a fraction?)");
        return static_cast<Address>(value);
    }

    // ---- memory --------------------------------------------------------------

    double Interpreter::loadDouble(Address address) const {
        std::uint64_t bits = 0;
        for (Address b = 0; b < static_cast<Address>(SLOT_SIZE); ++b) {
            auto it = m_memory.find(address + b);
            const std::uint64_t byte = it == m_memory.end() ? 0 : it->second;
            bits |= byte << (8 * b);
        }
        return std::bit_cast<double>(bits);
    }

    void Interpreter::storeDouble(Address address, double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (Address b = 0; b < static_cast<Address>(SLOT_SIZE); ++b)
            m_memory[address + b] = static_cast<std::uint8_t>(bits >> (8 * b));
    }

    // Read everything before writing anything, so overlapping ranges copy right.
    void Interpreter::copyBytes(Address dst, Address src, std::size_t size) {
        std::vector<std::uint8_t> bytes(size);
        for (std::size_t i = 0; i < size; ++i) {
            auto it = m_memory.find(src + static_cast<Address>(i));
            bytes[i] = it == m_memory.end() ? 0 : it->second;
        }
        for (std::size_t i = 0; i < size; ++i)
            m_memory[dst + static_cast<Address>(i)] = bytes[i];
    }

    bool Interpreter::sameBytes(Address a, Address b, std::size_t size) const {
        for (std::size_t i = 0; i < size; ++i) {
            auto x = m_memory.find(a + static_cast<Address>(i));
            auto y = m_memory.find(b + static_cast<Address>(i));
            if ((x == m_memory.end() ? 0 : x->second) != (y == m_memory.end() ? 0 : y->second))
                return false;
        }
        return true;
    }

    void Interpreter::printData() const {
        for (const LinDataChunk &data : m_lin.dataChunks()) {
            std::string shown;
            if (data.init) {
                shown = "\"" + *data.init + "\"";
            } else {
                const Address base = m_dataLabels.at(data.label.name);
                for (std::size_t off = 0; off < data.size; off += SLOT_SIZE)
                    shown += (off ? ", " : "") + number(loadDouble(base + static_cast<Address>(off)));
                if (data.size > SLOT_SIZE)
                    shown = "[" + shown + "]";
            }
            std::cout << "  " << nameText(data.label.name) << dimText(" = ") << shown << std::endl;
        }
    }

    // ---- expressions ---------------------------------------------------------

    void Interpreter::visit(ImcCONST &e) { m_value = e.value; }

    void Interpreter::visit(ImcNAME &e) {
        auto it = m_dataLabels.find(e.label.name);
        if (it == m_dataLabels.end())
            throw std::runtime_error("NAME " + e.label.name + " is not a data label");
        m_value = static_cast<double>(it->second);
    }

    void Interpreter::visit(ImcTEMP &e) {
        auto it = m_temps.find(e.temp.id);
        if (it == m_temps.end())
            throw std::runtime_error("read of " + e.temp.toString() + " before anything was stored in it");
        m_value = it->second;
    }

    void Interpreter::visit(ImcMEM &e) {
        if (e.size > SLOT_SIZE)
            throw std::runtime_error("a " + std::to_string(e.size) + "B value cannot be used as a number");
        m_value = loadDouble(toAddress(eval(*e.addr)));
    }

    void Interpreter::visit(ImcUNOP &e) {
        const double operand = eval(*e.expr);
        m_value = e.oper == ImcUNOP::Oper::NEG ? -operand : (operand == 0 ? 1 : 0);
    }

    void Interpreter::visit(ImcBINOP &e) {
        // Records and arrays compare equal when every byte does.
        const auto *wide = dynamic_cast<ImcMEM *>(e.fst.get());
        if (wide && wide->size > SLOT_SIZE
            && (e.oper == ImcBINOP::Oper::EQU || e.oper == ImcBINOP::Oper::NEQ)) {
            const bool same = sameBytes(wideAddress(*e.fst), wideAddress(*e.snd), wide->size);
            m_value = (e.oper == ImcBINOP::Oper::EQU) == same ? 1 : 0;
            return;
        }

        const double a = eval(*e.fst);
        const double b = eval(*e.snd);
        switch (e.oper) {
            case ImcBINOP::Oper::OR: m_value = (a != 0 || b != 0) ? 1 : 0; return;
            case ImcBINOP::Oper::AND: m_value = (a != 0 && b != 0) ? 1 : 0; return;
            case ImcBINOP::Oper::EQU: m_value = a == b ? 1 : 0; return;
            case ImcBINOP::Oper::NEQ: m_value = a != b ? 1 : 0; return;
            case ImcBINOP::Oper::LTH: m_value = a < b ? 1 : 0; return;
            case ImcBINOP::Oper::GTH: m_value = a > b ? 1 : 0; return;
            case ImcBINOP::Oper::LEQ: m_value = a <= b ? 1 : 0; return;
            case ImcBINOP::Oper::GEQ: m_value = a >= b ? 1 : 0; return;
            case ImcBINOP::Oper::ADD: m_value = a + b; return;
            case ImcBINOP::Oper::SUB: m_value = a - b; return;
            case ImcBINOP::Oper::MUL: m_value = a * b; return;
            case ImcBINOP::Oper::DIV:
                if (b == 0)
                    throw std::runtime_error("division by zero");
                m_value = a / b;
                return;
            case ImcBINOP::Oper::MOD:
                if (b == 0)
                    throw std::runtime_error("modulo by zero");
                m_value = std::fmod(a, b);
                return;
        }
    }

    void Interpreter::visit(ImcCALL &e) {
        // Work out every argument before writing any, so evaluating one cannot
        // overwrite another already placed in the block.
        struct Arg {
            std::size_t offset, size;
            double value;   // a one-slot argument
            Address source; // where a wider one is copied from
        };
        std::vector<Arg> args;
        args.reserve(e.args.size());
        for (std::size_t i = 0; i < e.args.size(); ++i) {
            if (e.sizes[i] > SLOT_SIZE)
                args.push_back({e.offsets[i], e.sizes[i], 0, wideAddress(*e.args[i])});
            else
                args.push_back({e.offsets[i], e.sizes[i], eval(*e.args[i]), 0});
        }

        for (const Arg &arg : args) {
            const Address at = m_sp + static_cast<Address>(arg.offset);
            if (arg.size > SLOT_SIZE)
                copyBytes(at, arg.source, arg.size);
            else
                storeDouble(at, arg.value);
        }

        m_value = call(e.label.name);
    }

    void Interpreter::visit(ImcSEXPR &) {
        throw std::runtime_error("SEXPR left in linearized code");
    }

    // ---- statements ----------------------------------------------------------

    void Interpreter::visit(ImcMOVE &s) {
        if (auto *mem = dynamic_cast<ImcMEM *>(s.dst.get())) {
            const Address dst = toAddress(eval(*mem->addr));
            if (mem->size > SLOT_SIZE)
                copyBytes(dst, wideAddress(*s.src), mem->size);
            else
                storeDouble(dst, eval(*s.src));
            return;
        }
        if (auto *temp = dynamic_cast<ImcTEMP *>(s.dst.get())) {
            m_temps[temp->temp.id] = eval(*s.src);
            return;
        }
        throw std::runtime_error("MOVE into neither a MEM nor a TEMP: " + s.toString());
    }

    void Interpreter::visit(ImcESTMT &s) { eval(*s.expr); }
    void Interpreter::visit(ImcJUMP &s) { m_jump = s.label.name; }
    void Interpreter::visit(ImcCJUMP &s) { m_jump = eval(*s.cond) != 0 ? s.pos.name : s.neg.name; }
    void Interpreter::visit(ImcLABEL &) {}

    void Interpreter::visit(ImcSTMTS &) {
        throw std::runtime_error("STMTS left in linearized code");
    }
}
