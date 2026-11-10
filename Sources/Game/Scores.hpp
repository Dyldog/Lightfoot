#pragma once

#include <string>
#include <vector>

namespace lightfoot {

/// The high-score table, kept as Lightfoot/scores.json on the card.
class Scores {
public:
    struct Entry {
        int score = 0;
        int level = 1;
        int combo = 0;
        std::string date; // "2026-11-10"
        std::string song; // empty for the endless course
        int difficulty = 1;
    };
    static constexpr int kKeep = 8;

    void load();
    void save() const;
    /// Inserts a run; returns its rank (0 is best) or -1 if it did not make the table.
    int add(int score, int level, int combo, const std::string& song = std::string(), int difficulty = 1);
    int best() const { return entries_.empty() ? 0 : entries_.front().score; }
    const std::vector<Entry>& entries() const { return entries_; }
    void clear() { entries_.clear(); }

private:
    std::vector<Entry> entries_;
};

} // namespace lightfoot
