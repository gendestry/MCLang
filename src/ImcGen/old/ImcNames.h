//
// Created by bobi on 15. 9. 26.
//
//  The two kinds of names intermediate code refers to: labels (places in the
//  code or data segment) and temporaries (an unbounded supply of registers).
//  Fresh ones come from a counter, so every generated name is unique.

#pragma once
#include <cstddef>
#include <string>

namespace Basic {
    // A named address. Functions and globals keep their source name; labels made
    // up for jumps (if/while/for) are numbered.
    struct ImcLabel {
        std::string name;

        explicit ImcLabel(std::string name) : name(std::move(name)) {}

        static ImcLabel fresh() {
            static std::size_t next = 0;
            return ImcLabel("L" + std::to_string(next++));
        }

        bool operator==(const ImcLabel &) const = default;
    };

    // A register that does not exist yet: the code generator maps these onto real
    // ones later. FP and RV are ordinary temporaries every frame agrees on.
    struct ImcTemp {
        std::size_t id = 0;

        explicit ImcTemp(std::size_t id) : id(id) {}

        static ImcTemp fresh() {
            static std::size_t next = 2; // 0 and 1 are FP and RV
            return ImcTemp(next++);
        }

        static ImcTemp FP() { return ImcTemp(0); }
        static ImcTemp RV() { return ImcTemp(1); }

        std::string toString() const {
            if (id == 0)
                return "FP";
            if (id == 1)
                return "RV";
            return "T" + std::to_string(id);
        }

        bool operator==(const ImcTemp &) const = default;
    };
}
