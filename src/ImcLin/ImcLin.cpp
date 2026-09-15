//
// Created by bobi on 15. 9. 26.
//

#include "ImcLin/ImcLin.h"

#include <iostream>

#include "ImcGen/ImcGen.h"
#include "ImcGen/data/stmt/ImcCJUMP.h"
#include "ImcGen/data/stmt/ImcJUMP.h"
#include "ImcGen/data/stmt/ImcLABEL.h"
#include "ImcLin/StmtCanonizer.h"
#include "Memory.h"
#include "Utils/Colors/Font.h"

namespace Basic {
    namespace {
        std::string nameText(const std::string &name) {
            return Utils::Font::colorYellow + "'" + name + "'" + Utils::Font::colorReset;
        }
        std::string dimText(const std::string &text) {
            return Utils::Font::colorDim + text + Utils::Font::colorReset;
        }
    }

    void ImcLin::print(const std::string &message) const {
        if (!m_print)
            return;
        std::cout << "  " << message << std::endl;
    }

    void ImcLin::compute(const Program &program, const Memory &memory, ImcGen &imcGen) {
        m_data.clear();
        m_code.clear();

        collectData(program, memory);
        for (const ImcGen::String &s : imcGen.strings()) {
            // One byte per character plus the terminating zero.
            const LinDataChunk &chunk = m_data.emplace_back(s.label, s.value.size() + 1, s.value);
            print(dimText("data ") + chunk.toString());
        }

        for (ImcGen::Function &f : imcGen.functions())
            addCode(*f.frame, std::move(f.body), f.entry, f.exit);
    }

    void ImcLin::collectData(const Program &program, const Memory &memory) {
        for (const DeclPtr &d : program) {
            const auto *var = dynamic_cast<const VarDecl *>(d.get());
            if (!var)
                continue;
            const auto *abs = dynamic_cast<const MemAbsAccess *>(memory.accessOf(var));
            if (!abs)
                continue;
            const LinDataChunk &chunk = m_data.emplace_back(ImcLabel(abs->label), abs->size);
            print(dimText("data ") + chunk.toString());
        }
    }

    void ImcLin::addCode(const MemFrame &frame, ImcStmtPtr body, ImcLabel entry, ImcLabel exit) {
        std::vector<ImcStmtPtr> stmts;
        stmts.push_back(std::make_unique<ImcLABEL>(entry));
        if (body)
            for (ImcStmtPtr &s : StmtCanonizer().canonize(*body))
                stmts.push_back(std::move(s));
        // A body that can fall off its end still has to reach the epilogue. One
        // that already ends in a jump (a trailing `return`) cannot fall off.
        if (!dynamic_cast<ImcJUMP *>(stmts.back().get()))
            stmts.push_back(std::make_unique<ImcJUMP>(exit));

        const LinCodeChunk &chunk =
            m_code.emplace_back(&frame, std::move(entry), std::move(exit), linearize(std::move(stmts)));
        printCode(chunk);
    }

    // Reads like assembly: labels flush left with a colon, statements indented
    // under them. The function label is where a CALL lands (the prologue goes
    // there); the exit label is where the epilogue goes.
    void ImcLin::printCode(const LinCodeChunk &chunk) const {
        if (!m_print)
            return;
        const MemFrame &frame = *chunk.frame;
        print("");
        print(Utils::Font::colorYellow + frame.label + ":" + Utils::Font::colorReset
              + dimText("    # prologue -- " + frame.toString()));
        for (const ImcStmtPtr &s : chunk.stmts) {
            if (const auto *label = dynamic_cast<const ImcLABEL *>(s.get()))
                print(label->label.name + ":");
            else
                print("    " + s->toString());
        }
        print(chunk.exit.name + ":" + dimText("    # epilogue"));
    }

    std::vector<ImcStmtPtr> ImcLin::linearize(std::vector<ImcStmtPtr> stmts) {
        std::vector<ImcStmtPtr> out;
        out.reserve(stmts.size());
        for (ImcStmtPtr &s : stmts) {
            auto *cjump = dynamic_cast<ImcCJUMP *>(s.get());
            if (!cjump) {
                out.push_back(std::move(s));
                continue;
            }
            ImcLabel neg = cjump->neg;
            cjump->neg = ImcLabel::fresh();
            ImcLabel fall = cjump->neg;
            out.push_back(std::move(s));
            out.push_back(std::make_unique<ImcLABEL>(std::move(fall)));
            out.push_back(std::make_unique<ImcJUMP>(std::move(neg)));
        }
        return out;
    }
}
