//
// Created by bobi on 15. 9. 26.
//

#include "ImcLin/ImcLin.h"

#include <iostream>

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
        stmts.push_back(std::make_unique<ImcJUMP>(exit));

        const LinCodeChunk &chunk =
            m_code.emplace_back(&frame, std::move(entry), std::move(exit), linearize(std::move(stmts)));

        print(dimText("code ") + nameText(frame.label) + dimText(" entry " + chunk.entry.name + ", exit "
                                                                 + chunk.exit.name));
        for (const ImcStmtPtr &s : chunk.stmts)
            print("  " + s->toString());
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
