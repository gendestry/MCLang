//
// Created by bobi on 15. 9. 26.
//
//  A named address. Functions and globals keep their source name; labels made
//  up for jumps (if/while/for) are numbered, so every fresh one is unique.

#pragma once
#include <cstddef>
#include <string>

namespace Basic {
    struct ImcLabel {
        std::string name;

        explicit ImcLabel(std::string name) : name(std::move(name)) {}

        static ImcLabel fresh() {
            static std::size_t next = 0;
            return ImcLabel("L" + std::to_string(next++));
        }

        bool operator==(const ImcLabel &) const = default;
    };
}
