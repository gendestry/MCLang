//
// Created by bobi on 15. 9. 26.
//
//  One function body, flattened: a list of plain statements with no nesting,
//  starting at the entry label (where the prologue jumps to) and ending with a
//  jump to the exit label (where the epilogue starts).

#pragma once
#include <vector>

#include "Mem.h"
#include "ImcGen/data/names/ImcLabel.h"
#include "ImcGen/data/stmt/ImcStmt.h"

namespace Basic {
    struct LinCodeChunk {
        const MemFrame *frame;
        ImcLabel entry;
        ImcLabel exit;
        std::vector<ImcStmtPtr> stmts;

        LinCodeChunk(const MemFrame *frame, ImcLabel entry, ImcLabel exit, std::vector<ImcStmtPtr> stmts)
            : frame(frame), entry(std::move(entry)), exit(std::move(exit)), stmts(std::move(stmts)) {}
    };
}
