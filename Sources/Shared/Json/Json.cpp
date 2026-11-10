#include "Shared/Json/Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lightfoot {

namespace {

const Json kNull;

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string error;

    void skip() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    bool fail(const char* what) {
        if (error.empty()) error = std::string(what) + " at " + std::to_string(i);
        return false;
    }
    bool parseValue(Json& out) {
        skip();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            std::string str;
            if (!parseString(str)) return false;
            out = Json(std::move(str));
            return true;
        }
        if (s.compare(i, 4, "true") == 0) {
            i += 4;
            out = Json(true);
            return true;
        }
        if (s.compare(i, 5, "false") == 0) {
            i += 5;
            out = Json(false);
            return true;
        }
        if (s.compare(i, 4, "null") == 0) {
            i += 4;
            out = Json();
            return true;
        }
        return parseNumber(out);
    }
    bool parseNumber(Json& out) {
        const char* start = s.c_str() + i;
        char* end = nullptr;
        const double v = std::strtod(start, &end);
        if (end == start) return fail("bad number");
        i += static_cast<size_t>(end - start);
        out = Json(v);
        return true;
    }
    bool parseString(std::string& out) {
        if (s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (i + 4 > s.size()) return fail("bad escape");
                const unsigned cp = static_cast<unsigned>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                i += 4;
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool parseArray(Json& out) {
        ++i;
        out = Json::array();
        skip();
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        for (;;) {
            Json v;
            if (!parseValue(v)) return false;
            out.push(std::move(v));
            skip();
            if (i >= s.size()) return fail("unterminated array");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == ']') {
                ++i;
                return true;
            }
            return fail("expected , or ]");
        }
    }
    bool parseObject(Json& out) {
        ++i;
        out = Json::object();
        skip();
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        for (;;) {
            skip();
            std::string key;
            if (!parseString(key)) return false;
            skip();
            if (i >= s.size() || s[i] != ':') return fail("expected :");
            ++i;
            Json v;
            if (!parseValue(v)) return false;
            out.set(key, std::move(v));
            skip();
            if (i >= s.size()) return fail("unterminated object");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == '}') {
                ++i;
                return true;
            }
            return fail("expected , or }");
        }
    }
};

void escape(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

} // namespace

const Json& Json::operator[](size_t i) const {
    return type_ == Type::Array && i < items_.size() ? items_[i] : kNull;
}

Json& Json::push(Json v) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        items_.clear();
        keys_.clear();
    }
    items_.push_back(std::move(v));
    return *this;
}

const Json& Json::operator[](const std::string& key) const {
    if (type_ != Type::Object) return kNull;
    for (size_t k = 0; k < keys_.size(); ++k)
        if (keys_[k] == key) return items_[k];
    return kNull;
}

bool Json::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (const auto& k : keys_)
        if (k == key) return true;
    return false;
}

Json& Json::set(const std::string& key, Json v) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        items_.clear();
        keys_.clear();
    }
    for (size_t k = 0; k < keys_.size(); ++k)
        if (keys_[k] == key) {
            items_[k] = std::move(v);
            return *this;
        }
    keys_.push_back(key);
    items_.push_back(std::move(v));
    return *this;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    const std::string pad(static_cast<size_t>(indent * (depth + 1)), ' ');
    const std::string padEnd(static_cast<size_t>(indent * depth), ' ');
    const char* nl = indent > 0 ? "\n" : "";
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
        char buf[32];
        if (std::fabs(number_ - std::round(number_)) < 1e-9 && std::fabs(number_) < 1e15)
            std::snprintf(buf, sizeof buf, "%.0f", number_);
        else
            std::snprintf(buf, sizeof buf, "%.6g", number_);
        out += buf;
        break;
    }
    case Type::String: escape(out, string_); break;
    case Type::Array:
        if (items_.empty()) {
            out += "[]";
            break;
        }
        out += "[";
        out += nl;
        for (size_t k = 0; k < items_.size(); ++k) {
            if (indent > 0) out += pad;
            items_[k].dumpTo(out, indent, depth + 1);
            if (k + 1 < items_.size()) out += ",";
            out += nl;
        }
        if (indent > 0) out += padEnd;
        out += "]";
        break;
    case Type::Object:
        if (keys_.empty()) {
            out += "{}";
            break;
        }
        out += "{";
        out += nl;
        for (size_t k = 0; k < keys_.size(); ++k) {
            if (indent > 0) out += pad;
            escape(out, keys_[k]);
            out += indent > 0 ? ": " : ":";
            items_[k].dumpTo(out, indent, depth + 1);
            if (k + 1 < keys_.size()) out += ",";
            out += nl;
        }
        if (indent > 0) out += padEnd;
        out += "}";
        break;
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

Json Json::parse(const std::string& text, std::string* error) {
    Parser p{text, 0, {}};
    Json v;
    if (!p.parseValue(v)) {
        if (error) *error = p.error;
        return Json();
    }
    return v;
}

} // namespace lightfoot
