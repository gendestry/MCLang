//
// Created by bobi on 6. 9. 26.
//
//  The vocabulary the Memory phase produces: where a thing lives, and how much
//  room it takes. Nothing here walks the AST -- these are plain descriptions
//  that later phases (IMC lowering, then the prologue/epilogue emitter) read.

#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace Basic {
    // Everything the language can hold fits in one slot, and every record is a
    // whole number of slots, so no padding is ever needed.
    inline constexpr std::size_t SLOT_SIZE = 8;

    // ---- accesses ------------------------------------------------------------

    // Where a variable lives. Two kinds, and telling them apart is the whole
    // point of the phase: a global has one address for the life of the program,
    // a local has a different address per call and can only be described
    // relative to the frame pointer.
    struct MemAccess {
        virtual ~MemAccess() = default;
        std::size_t size = SLOT_SIZE;
        virtual std::string toString() const = 0;

    protected:
        explicit MemAccess(std::size_t size) : size(size) {}
    };

    // A fixed address in the data segment, named by a label the linker resolves.
    struct MemAbsAccess : MemAccess {
        std::string label;

        MemAbsAccess(std::string label, std::size_t size)
            : MemAccess(size), label(std::move(label)) {}

        std::string toString() const override {
            return "abs " + label + " (" + std::to_string(size) + "B)";
        }
    };

    // An offset from the frame pointer of the frame at `depth`. Negative offsets
    // are locals (below FP), non-negative ones are incoming parameters (above it).
    struct MemRelAccess : MemAccess {
        long long offset = 0;
        std::size_t depth = 0;

        MemRelAccess(long long offset, std::size_t depth, std::size_t size)
            : MemAccess(size), offset(offset), depth(depth) {}

        std::string toString() const override {
            const std::string sign = offset < 0 ? "-" : "+";
            return "FP" + sign + std::to_string(offset < 0 ? -offset : offset) + " @ depth "
                   + std::to_string(depth) + " (" + std::to_string(size) + "B)";
        }
    };

    // ---- frames --------------------------------------------------------------

    // One activation record. `locals` and `args` are the two blocks whose sizes
    // can only be known after the whole body has been walked: `locals` grows as
    // declarations are met, `args` is the widest outgoing argument block of any
    // call in the body (0 when the function calls nothing).
    struct MemFrame {
        std::string label;   // what a call jumps to
        std::size_t depth = 0; // 1 for a function declared at global level
        std::size_t locals = 0;
        std::size_t args = 0;

        // The saved old-FP and return-address slots sit between the two blocks.
        static constexpr std::size_t LINKAGE = 2 * SLOT_SIZE;

        std::size_t size() const { return locals + LINKAGE + args; }

        std::string toString() const {
            return label + ": depth " + std::to_string(depth) + ", locals "
                   + std::to_string(locals) + "B, args " + std::to_string(args) + "B, frame "
                   + std::to_string(size()) + "B";
        }
    };

    // ---- record layout -------------------------------------------------------

    // Field offsets within a record, in declaration order. Member access lowers
    // to `base address + offset`, so this is what makes `p.x` an address.
    struct MemLayout {
        struct Field {
            std::string name;
            std::size_t offset = 0;
            std::size_t size = 0;
        };

        std::size_t size = 0;
        std::vector<Field> fields;

        const Field *find(const std::string &name) const {
            for (const Field &f : fields)
                if (f.name == name)
                    return &f;
            return nullptr;
        }
    };
}
