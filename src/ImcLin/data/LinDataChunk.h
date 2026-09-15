//
// Created by bobi on 15. 9. 26.
//
//  A piece of the data segment: the storage behind one global variable (and,
//  once they get labels, one string literal).

#pragma once
#include <cstddef>
#include <optional>
#include <string>

#include "ImcGen/data/names/ImcLabel.h"

namespace Basic {
    struct LinDataChunk {
        ImcLabel label;
        std::size_t size = 0;
        std::optional<std::string> init; // initial bytes; none means zero-filled

        LinDataChunk(ImcLabel label, std::size_t size, std::optional<std::string> init = std::nullopt)
            : label(std::move(label)), size(size), init(std::move(init)) {}

        std::string toString() const {
            std::string out = label.name + " (" + std::to_string(size) + "B)";
            if (init)
                out += " = \"" + *init + "\"";
            return out;
        }
    };
}
