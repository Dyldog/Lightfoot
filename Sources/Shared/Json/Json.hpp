#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lightfoot {

/// A small JSON value for project files. Objects keep key order so saved
/// files diff cleanly between revisions.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(int v) : type_(Type::Number), number_(v) {}
    Json(double v) : type_(Type::Number), number_(v) {}
    Json(const char* s) : type_(Type::String), string_(s) {}
    Json(std::string s) : type_(Type::String), string_(std::move(s)) {}

    static Json array() {
        Json j;
        j.type_ = Type::Array;
        return j;
    }
    static Json object() {
        Json j;
        j.type_ = Type::Object;
        return j;
    }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double asNumber(double fallback = 0) const { return type_ == Type::Number ? number_ : fallback; }
    int asInt(int fallback = 0) const { return type_ == Type::Number ? static_cast<int>(number_) : fallback; }
    const std::string& asString() const { return string_; }
    std::string asString(const std::string& fallback) const { return type_ == Type::String ? string_ : fallback; }

    // Arrays
    size_t size() const { return type_ == Type::Array ? items_.size() : type_ == Type::Object ? keys_.size() : 0; }
    const Json& operator[](size_t i) const;
    Json& push(Json v);

    // Objects
    const Json& operator[](const std::string& key) const;
    bool has(const std::string& key) const;
    Json& set(const std::string& key, Json v);
    const std::vector<std::string>& keys() const { return keys_; }

    std::string dump(int indent = 2) const;
    /// Parses text. Returns a Null value and sets `error` on failure.
    static Json parse(const std::string& text, std::string* error = nullptr);

private:
    void dumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Json> items_;             // array items, or object values in key order
    std::vector<std::string> keys_;       // object keys
};

} // namespace lightfoot
