#include "Game/Scores.hpp"

#include <algorithm>
#include <ctime>

#include "Shared/Json/Json.hpp"
#include "Shared/Paths/Paths.hpp"

namespace lightfoot {

namespace {
std::string scoresPath() {
    return paths::join(paths::dataDir(), "scores.json");
}
} // namespace

void Scores::load() {
    entries_.clear();
    std::string text;
    if (!paths::readFile(scoresPath(), text)) return;
    const Json j = Json::parse(text);
    const Json& list = j["scores"];
    for (size_t i = 0; i < list.size(); ++i) {
        Entry e;
        e.score = list[i]["score"].asInt(0);
        e.level = list[i]["level"].asInt(1);
        e.combo = list[i]["combo"].asInt(0);
        e.date = list[i]["date"].asString("");
        e.song = list[i]["song"].asString("");
        e.difficulty = list[i]["difficulty"].asInt(1);
        entries_.push_back(e);
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.score > b.score; });
    if (entries_.size() > kKeep) entries_.resize(kKeep);
}

void Scores::save() const {
    Json list = Json::array();
    for (const Entry& e : entries_) {
        Json o = Json::object();
        o.set("score", e.score);
        o.set("level", e.level);
        o.set("combo", e.combo);
        o.set("date", e.date);
        o.set("song", e.song);
        o.set("difficulty", e.difficulty);
        list.push(o);
    }
    Json root = Json::object();
    root.set("scores", list);
    paths::makeDirs(paths::dataDir());
    paths::writeFileAtomic(scoresPath(), root.dump(2) + "\n");
}

int Scores::add(int score, int level, int combo, const std::string& song, int difficulty) {
    Entry e;
    e.song = song;
    e.difficulty = difficulty;
    e.score = score;
    e.level = level;
    e.combo = combo;
    const std::time_t now = std::time(nullptr);
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d", std::localtime(&now));
    e.date = stamp;
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& x) { return score > x.score; });
    const int rank = static_cast<int>(it - entries_.begin());
    if (rank >= kKeep) return -1;
    entries_.insert(it, e);
    if (entries_.size() > kKeep) entries_.resize(kKeep);
    save();
    return rank;
}

} // namespace lightfoot
