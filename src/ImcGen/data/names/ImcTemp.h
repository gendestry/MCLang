//
// Created by bobi on 15. 9. 26.
//
//  A register that does not exist yet: the code generator maps these onto real
//  ones later. FP and RV are ordinary temporaries every frame agrees on.

#pragma once
#include <cstddef>
#include <string>

namespace Basic {
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
