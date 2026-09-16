//
// Created by bobi on 16. 9. 26.
//

#include "ImcOpt/LinearPasses.h"

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "ImcGen/data/expr/ImcCONST.h"
#include "ImcGen/data/expr/ImcNAME.h"
#include "ImcOpt/ConstantFolder.h"
#include "ImcOpt/ImcClone.h"
#include "ImcOpt/ImcWalk.h"
#include "ImcOpt/Liveness.h"

namespace Basic {
    namespace {
        using Stmts = std::vector<ImcStmtPtr>;

        void erase(Stmts &stmts, std::size_t i) { stmts.erase(stmts.begin() + static_cast<std::ptrdiff_t>(i)); }

        bool tracked(const ImcTemp &temp) { return temp.id > ImcTemp::RV().id; }
    }

    // ---- CopyPropagation -----------------------------------------------------

    std::size_t CopyPropagation::run(LinCodeChunk &chunk) {
        std::size_t count = 0;
        std::unordered_map<std::size_t, ImcExprPtr> copies; // temp id -> TEMP, CONST or NAME

        for (ImcStmtPtr &s : chunk.stmts) {
            if (ImcWalk::asLabel(*s)) {
                copies.clear();
                continue;
            }

            ImcWalk::forEachRead(*s, [&](ImcExprPtr &slot) {
                const auto *temp = dynamic_cast<const ImcTEMP *>(slot.get());
                if (!temp)
                    return;
                if (auto it = copies.find(temp->temp.id); it != copies.end()) {
                    slot = clone(*it->second);
                    ++count;
                }
            });

            for (std::size_t id : Liveness::defs(*s)) {
                copies.erase(id);
                std::erase_if(copies, [id](const auto &entry) {
                    const auto *temp = dynamic_cast<const ImcTEMP *>(entry.second.get());
                    return temp && temp->temp.id == id;
                });
            }

            const auto *dst = ImcWalk::movedTemp(*s);
            if (!dst || !tracked(dst->temp))
                continue;
            const ImcExpr &src = *static_cast<ImcMOVE &>(*s).src;
            const auto *from = dynamic_cast<const ImcTEMP *>(&src);
            // RV changes at every call, so a copy of it can't stand in for it.
            if ((from && from->temp != ImcTemp::RV() && from->temp != dst->temp)
                || dynamic_cast<const ImcCONST *>(&src) || dynamic_cast<const ImcNAME *>(&src))
                copies.emplace(dst->temp.id, clone(src));
        }
        return count;
    }

    // ---- DeadCode ------------------------------------------------------------

    std::size_t DeadCode::run(LinCodeChunk &chunk) {
        Stmts &stmts = chunk.stmts;
        std::size_t count = 0;

        for (std::size_t i = 1; i < stmts.size();) {
            if (ImcWalk::isTerminator(*stmts[i - 1]) && !ImcWalk::asLabel(*stmts[i])) {
                erase(stmts, i);
                ++count;
            } else {
                ++i;
            }
        }

        bool changed = true;
        while (changed) {
            changed = false;
            const std::vector<Liveness::Temps> liveOut = Liveness::liveOut(chunk);
            // Backwards, so erasing doesn't move the statements still to be looked at.
            for (std::size_t i = stmts.size(); i-- > 0;) {
                if (auto *move = dynamic_cast<ImcMOVE *>(stmts[i].get())) {
                    const auto *dst = dynamic_cast<const ImcTEMP *>(move->dst.get());
                    if (!dst || !tracked(dst->temp) || liveOut[i].contains(dst->temp.id))
                        continue;
                    if (dynamic_cast<ImcCALL *>(move->src.get()))
                        stmts[i] = std::make_unique<ImcESTMT>(std::move(move->src)); // the call still runs
                    else if (!ImcWalk::hasCall(*move->src))
                        erase(stmts, i);
                    else
                        continue;
                    ++count;
                    changed = true;
                } else if (auto *estmt = dynamic_cast<ImcESTMT *>(stmts[i].get())) {
                    if (ImcWalk::hasCall(*estmt->expr))
                        continue;
                    erase(stmts, i);
                    ++count;
                    changed = true;
                }
            }
        }
        return count;
    }

    // ---- JumpThreading -------------------------------------------------------

    std::size_t JumpThreading::run(LinCodeChunk &chunk) {
        Stmts &stmts = chunk.stmts;
        std::size_t count = 0;

        // Where each label leads when its block is nothing but a jump.
        std::unordered_map<std::string, std::string> forward;
        for (std::size_t i = 0; i + 1 < stmts.size(); ++i) {
            const auto *label = ImcWalk::asLabel(*stmts[i]);
            const auto *jump = dynamic_cast<const ImcJUMP *>(stmts[i + 1].get());
            if (label && jump && label->label.name != jump->label.name && label->label.name != chunk.entry.name)
                forward.emplace(label->label.name, jump->label.name);
        }
        auto resolve = [&](std::string name) {
            std::unordered_set<std::string> seen; // a loop of empty jumps stops somewhere in the loop
            for (auto it = forward.find(name); it != forward.end() && seen.insert(name).second;
                 it = forward.find(name))
                name = it->second;
            return name;
        };
        for (ImcStmtPtr &s : stmts)
            ImcWalk::forEachTarget(*s, [&](ImcLabel &label) {
                std::string to = resolve(label.name);
                if (to != label.name) {
                    label = ImcLabel(std::move(to));
                    ++count;
                }
            });

        for (ImcStmtPtr &s : stmts) {
            auto *cjump = dynamic_cast<ImcCJUMP *>(s.get());
            if (!cjump)
                continue;
            if (cjump->pos == cjump->neg) {
                s = std::make_unique<ImcJUMP>(cjump->pos);
                ++count;
            } else if (const auto *c = dynamic_cast<const ImcCONST *>(cjump->cond.get())) {
                s = std::make_unique<ImcJUMP>(c->value != 0 ? cjump->pos : cjump->neg);
                ++count;
            }
        }

        for (std::size_t i = 0; i + 1 < stmts.size();) {
            const auto *jump = dynamic_cast<const ImcJUMP *>(stmts[i].get());
            const auto *next = ImcWalk::asLabel(*stmts[i + 1]);
            if (jump && next && jump->label == next->label) {
                erase(stmts, i);
                ++count;
            } else {
                ++i;
            }
        }

        std::unordered_map<std::string, std::size_t> refs;
        for (ImcStmtPtr &s : stmts)
            ImcWalk::forEachTarget(*s, [&](ImcLabel &label) { ++refs[label.name]; });

        // The entry label is where the prologue calls in, so it always stays.
        for (std::size_t i = 1; i < stmts.size();) {
            const auto *label = ImcWalk::asLabel(*stmts[i]);
            if (!label || refs[label->label.name] > 0 || label->label.name == chunk.entry.name) {
                ++i;
                continue;
            }
            const bool reachable = !ImcWalk::isTerminator(*stmts[i - 1]);
            erase(stmts, i);
            ++count;
            if (!reachable)
                while (i < stmts.size() && !ImcWalk::asLabel(*stmts[i]))
                    erase(stmts, i);
        }
        return count;
    }

    // ---- CommonSubexpr -------------------------------------------------------

    std::size_t CommonSubexpr::run(LinCodeChunk &chunk) {
        struct Available {
            std::string key; // the expression, printed
            ImcTemp temp;    // where its value is
            Liveness::Temps reads;
            bool memory;
        };
        std::vector<Available> available;
        std::size_t count = 0;

        for (ImcStmtPtr &s : chunk.stmts) {
            if (ImcWalk::asLabel(*s)) {
                available.clear();
                continue;
            }

            auto *move = dynamic_cast<ImcMOVE *>(s.get());
            const auto *dst = move ? dynamic_cast<const ImcTEMP *>(move->dst.get()) : nullptr;
            bool computes = dst && tracked(dst->temp) && !ImcWalk::hasCall(*move->src)
                            && (dynamic_cast<const ImcBINOP *>(move->src.get())
                                || dynamic_cast<const ImcUNOP *>(move->src.get())
                                || dynamic_cast<const ImcMEM *>(move->src.get()));
            const std::string key = computes ? move->src->toString() : "";
            if (computes) {
                for (const Available &a : available) {
                    if (a.key == key && a.temp != dst->temp) {
                        move->src = std::make_unique<ImcTEMP>(a.temp);
                        ++count;
                        computes = false;
                        break;
                    }
                }
            }

            for (std::size_t id : Liveness::defs(*s))
                std::erase_if(available, [id](const Available &a) { return a.temp.id == id || a.reads.contains(id); });
            if (ImcWalk::writesMemory(*s))
                std::erase_if(available, [](const Available &a) { return a.memory; });

            if (computes) {
                Liveness::Temps reads = Liveness::uses(*s);
                if (!reads.contains(dst->temp.id))
                    available.push_back({key, dst->temp, std::move(reads), ImcWalk::hasMem(*move->src)});
            }
        }
        return count;
    }

    // ---- LoopInvariant -------------------------------------------------------

    namespace {
        bool mayFail(const ImcExpr &expr) {
            return ImcWalk::any(expr, [](const ImcExpr &e) {
                const auto *binop = dynamic_cast<const ImcBINOP *>(&e);
                return binop && (binop->oper == ImcBINOP::Oper::DIV || binop->oper == ImcBINOP::Oper::MOD);
            });
        }

        bool hoistOne(LinCodeChunk &chunk) {
            Stmts &stmts = chunk.stmts;
            std::unordered_map<std::string, std::size_t> labels;
            for (std::size_t i = 0; i < stmts.size(); ++i)
                if (const auto *label = ImcWalk::asLabel(*stmts[i]))
                    labels[label->label.name] = i;
            const std::vector<Liveness::Temps> liveOut = Liveness::liveOut(chunk);

            for (std::size_t back = 0; back < stmts.size(); ++back) {
                const auto *jump = dynamic_cast<const ImcJUMP *>(stmts[back].get());
                auto top = jump ? labels.find(jump->label.name) : labels.end();
                if (top == labels.end() || top->second == 0 || top->second >= back)
                    continue;
                const std::size_t head = top->second;
                if (ImcWalk::isTerminator(*stmts[head - 1]))
                    continue; // not fallen into: nowhere to put the hoisted code

                // Only entered through the top, and only from just in front of it.
                std::unordered_set<std::string> inside;
                for (std::size_t i = head; i <= back; ++i)
                    if (const auto *label = ImcWalk::asLabel(*stmts[i]))
                        inside.insert(label->label.name);
                bool closed = true;
                for (std::size_t i = 0; i < stmts.size() && closed; ++i) {
                    if (i >= head && i <= back)
                        continue;
                    ImcWalk::forEachTarget(*stmts[i], [&](ImcLabel &label) {
                        closed = closed && !inside.contains(label.name);
                    });
                }
                if (!closed)
                    continue;

                std::unordered_map<std::size_t, std::size_t> writes;
                bool memoryWritten = false;
                for (std::size_t i = head; i <= back; ++i) {
                    for (std::size_t id : Liveness::defs(*stmts[i]))
                        ++writes[id];
                    memoryWritten = memoryWritten || ImcWalk::writesMemory(*stmts[i]);
                }

                for (std::size_t i = head + 1; i < back; ++i) {
                    const auto *dst = ImcWalk::movedTemp(*stmts[i]);
                    if (!dst || !tracked(dst->temp) || writes[dst->temp.id] != 1
                        || liveOut[head].contains(dst->temp.id))
                        continue;
                    const ImcExpr &src = *static_cast<ImcMOVE &>(*stmts[i]).src;
                    if (ImcWalk::hasCall(src) || mayFail(src) || (memoryWritten && ImcWalk::hasMem(src)))
                        continue;
                    bool invariant = true;
                    for (std::size_t id : Liveness::uses(*stmts[i]))
                        invariant = invariant && !writes.contains(id);
                    if (!invariant)
                        continue;

                    ImcStmtPtr hoisted = std::move(stmts[i]);
                    erase(stmts, i);
                    stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(head), std::move(hoisted));
                    return true;
                }
            }
            return false;
        }
    }

    std::size_t LoopInvariant::run(LinCodeChunk &chunk) {
        std::size_t count = 0;
        while (hoistOne(chunk))
            ++count;
        return count;
    }

    // ---- LinOptimizer --------------------------------------------------------

    std::size_t LinOptimizer::run(LinCodeChunk &chunk) {
        static constexpr int MAX_ROUNDS = 32; // a safety net; a few rounds always settle it
        ConstantFolder folder;
        std::size_t total = 0;
        for (int round = 0; round < MAX_ROUNDS; ++round) {
            std::size_t changes = 0;
            for (ImcStmtPtr &s : chunk.stmts)
                changes += folder.run(chunk.frame->label, s);
            changes += CommonSubexpr::run(chunk);
            changes += CopyPropagation::run(chunk);
            changes += DeadCode::run(chunk);
            changes += LoopInvariant::run(chunk);
            changes += JumpThreading::run(chunk);
            total += changes;
            if (changes == 0)
                break;
        }
        return total;
    }
}
