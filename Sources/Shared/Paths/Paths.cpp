#include "Shared/Paths/Paths.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace lightfoot::paths {

std::string cardRoot() {
    if (const char* root = std::getenv("LIGHTFOOT_ROOT")) return root;
    if (const char* app = std::getenv("MPC_APP_DIR")) {
        // MPC_APP_DIR is <card>/autorun or <card>/autorun/apps/<name>/autorun.
        std::string dir = app;
        const std::string::size_type at = dir.find("/autorun");
        if (at != std::string::npos && at > 0) return dir.substr(0, at);
        return dir;
    }
    return "build/sdcard";
}

std::string dataDir() {
    return join(cardRoot(), "Lightfoot");
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

bool exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

bool makeDirs(const std::string& path) {
    std::string cur;
    std::stringstream ss(path);
    std::string part;
    if (!path.empty() && path[0] == '/') cur = "/";
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        cur = join(cur, part);
        ::mkdir(cur.c_str(), 0755);
    }
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isDirectory(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<std::string> listFiles(const std::string& dir, const std::string& suffix) {
    std::vector<std::string> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name.empty() || name[0] == '.' || name.size() < suffix.size()) continue;
        std::string tail = name.substr(name.size() - suffix.size());
        std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (tail != suffix) continue;
        if (isDirectory(join(dir, name))) continue;
        out.push_back(name);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << data;
        if (!f) return false;
    }
    return ::rename(tmp.c_str(), path.c_str()) == 0;
}

} // namespace lightfoot::paths
