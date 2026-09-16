//
// Created by bobi on 16. 9. 26.
//

#include "McGen/McPeephole.h"

#include <climits>
#include <cstddef>
#include <optional>

namespace Basic {
    namespace {
        using Tokens = std::vector<std::string>;

        // Commands are written with single spaces, so splitting and joining on one
        // space gives back the same text.
        Tokens split(const std::string &line) {
            Tokens out;
            std::size_t start = 0;
            while (true) {
                const std::size_t space = line.find(' ', start);
                out.push_back(line.substr(start, space - start));
                if (space == std::string::npos)
                    return out;
                start = space + 1;
            }
        }

        std::string join(const Tokens &tokens) {
            std::string out;
            for (std::size_t i = 0; i < tokens.size(); ++i)
                out += (i ? " " : "") + tokens[i];
            return out;
        }

        bool isComment(const std::string &line) { return line.empty() || line[0] == '#'; }
        bool isCall(const std::string &line) { return line.starts_with("function ") || line.contains(" function "); }
        bool isReturn(const std::string &line) { return line.starts_with("return") || line.contains(" return "); }

        bool isScratch(const std::string &holder) {
            if (holder.size() < 3 || !holder.starts_with("$e"))
                return false;
            for (std::size_t i = 2; i < holder.size(); ++i)
                if (holder[i] < '0' || holder[i] > '9')
                    return false;
            return true;
        }

        // A holder is always written `<name> mcl`.
        std::size_t mentions(const Tokens &t, const std::string &holder) {
            std::size_t count = 0;
            for (std::size_t i = 0; i + 1 < t.size(); ++i)
                count += t[i] == holder && t[i + 1] == "mcl";
            return count;
        }

        void rename(Tokens &t, const std::string &from, const std::string &to) {
            for (std::size_t i = 0; i + 1 < t.size(); ++i)
                if (t[i] == from && t[i + 1] == "mcl")
                    t[i] = to;
        }

        bool isScoreboard(const Tokens &t, const char *verb) {
            return t.size() >= 4 && t[0] == "scoreboard" && t[1] == "players" && t[2] == verb;
        }

        // `set H mcl N`: the constant, when the line is one for `holder`.
        std::optional<long long> setValue(const Tokens &t, const std::string &holder) {
            if (t.size() != 6 || !isScoreboard(t, "set") || t[3] != holder)
                return std::nullopt;
            return std::stoll(t[5]);
        }

        // Whether the line gives `holder` a new value without reading the old one.
        bool defines(const Tokens &t, const std::string &holder) {
            if (setValue(t, holder))
                return true;
            if (t.size() == 8 && isScoreboard(t, "operation") && t[3] == holder && t[5] == "=")
                return t[6] != holder;
            return t.size() > 6 && t[0] == "execute" && t[1] == "store" && t[2] == "result" && t[3] == "score"
                   && t[4] == holder && t[6] == "run" && mentions(t, holder) == 1;
        }

        // Whether nothing from line `from` on reads the scratch's current value.
        bool dead(const std::vector<std::string> &lines, std::size_t from, const std::string &scratch) {
            for (std::size_t i = from; i < lines.size(); ++i) {
                if (isComment(lines[i]))
                    continue;
                const Tokens t = split(lines[i]);
                if (mentions(t, scratch))
                    return defines(t, scratch);
            }
            return true;
        }

        bool fitsInt(long long value) { return value >= INT_MIN && value <= INT_MAX; }

        // `add H n`, or `remove H -n`: add only takes a non-negative amount.
        std::optional<std::string> addLine(const std::string &holder, long long amount) {
            if (!fitsInt(amount) || !fitsInt(-amount))
                return std::nullopt;
            return amount < 0 ? "scoreboard players remove " + holder + " mcl " + std::to_string(-amount)
                              : "scoreboard players add " + holder + " mcl " + std::to_string(amount);
        }

        // The `matches` range for `score OP c`.
        std::optional<std::string> range(const std::string &op, long long c) {
            if (op == "=") return std::to_string(c);
            if (op == "<=") return ".." + std::to_string(c);
            if (op == ">=") return std::to_string(c) + "..";
            if (op == "<" && c > INT_MIN) return ".." + std::to_string(c - 1);
            if (op == ">" && c < INT_MAX) return std::to_string(c + 1) + "..";
            return std::nullopt;
        }

        // `set X c`, then the one line reading X: use c there instead.
        bool foldConstant(std::vector<std::string> &lines, std::size_t i) {
            const Tokens set = split(lines[i]);
            if (set.size() != 6 || !isScoreboard(set, "set") || !isScratch(set[3]))
                return false;
            const std::string &x = set[3];
            const long long c = std::stoll(set[5]);

            for (std::size_t j = i + 1; j < lines.size(); ++j) {
                if (isComment(lines[j]))
                    continue;
                const Tokens t = split(lines[j]);
                // The line reading X may still run a function after it (`... run
                // return run function`): X is read first. A call before it may not.
                if (!mentions(t, x)) {
                    if (isCall(lines[j]))
                        return false;
                    continue;
                }
                if (mentions(t, x) != 1 || !dead(lines, j + 1, x))
                    return false;

                std::optional<std::string> rewritten;
                // D += X, D -= X
                if (t.size() == 8 && isScoreboard(t, "operation") && t[6] == x && t[3] != x
                    && (t[5] == "+=" || t[5] == "-="))
                    rewritten = addLine(t[3], t[5] == "+=" ? c : -c);
                // execute if|unless score A OP X run ...
                if (t.size() > 8 && t[0] == "execute" && (t[1] == "if" || t[1] == "unless") && t[2] == "score"
                    && t[6] == x && t[8] == "run" && t[3] != x) {
                    if (const std::optional<std::string> r = range(t[5], c)) {
                        Tokens out(t.begin(), t.begin() + 5);
                        out.push_back("matches");
                        out.push_back(*r);
                        out.insert(out.end(), t.begin() + 8, t.end());
                        rewritten = join(out);
                    }
                }
                if (!rewritten)
                    return false;
                lines[j] = *rewritten;
                lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
            return false;
        }

        // X computed, then `Y = X`: compute it into Y directly.
        bool coalesce(std::vector<std::string> &lines, std::size_t j) {
            const Tokens copy = split(lines[j]);
            if (copy.size() != 8 || !isScoreboard(copy, "operation") || copy[5] != "=")
                return false;
            const std::string &y = copy[3], &x = copy[6];
            if (!isScratch(x) || x == y || !dead(lines, j + 1, x))
                return false;

            // Back to where X gets its value. Y must not be touched on the way, or
            // writing it early would change what those lines see.
            for (std::size_t k = j; k-- > 0;) {
                if (isComment(lines[k]))
                    continue;
                if (isCall(lines[k]) || isReturn(lines[k]))
                    return false;
                const Tokens t = split(lines[k]);
                // `X = Y` itself may name Y: Y still holds that value when X starts
                // out as it, so renaming leaves a `Y = Y` for noOp to delete.
                const bool copiesY = t.size() == 8 && isScoreboard(t, "operation") && t[3] == x && t[5] == "="
                                     && t[6] == y;
                if (mentions(t, y) && !copiesY)
                    return false;
                if (!defines(t, x))
                    continue;

                for (std::size_t m = k; m < j; ++m) {
                    if (isComment(lines[m]))
                        continue;
                    Tokens renamed = split(lines[m]);
                    rename(renamed, x, y);
                    lines[m] = join(renamed);
                }
                lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(j));
                return true;
            }
            return false;
        }

        // add X 0, remove X 0, X = X
        bool noOp(const std::vector<std::string> &lines, std::size_t i) {
            const Tokens t = split(lines[i]);
            if (t.size() == 6 && (isScoreboard(t, "add") || isScoreboard(t, "remove")))
                return t[5] == "0";
            return t.size() == 8 && isScoreboard(t, "operation") && t[5] == "=" && t[3] == t[6];
        }
    }

    std::size_t McPeephole::run(std::vector<std::string> &lines) {
        std::size_t rewrites = 0;
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (isComment(lines[i]))
                    continue;
                if (noOp(lines, i)) {
                    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
                } else if (!foldConstant(lines, i) && !coalesce(lines, i)) {
                    continue;
                }
                ++rewrites;
                changed = true;
                break; // indices moved; start over
            }
        }
        return rewrites;
    }
}
