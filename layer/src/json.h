// A JSON writer and a reader for flat request objects. Header only.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace efvk {

class JsonWriter {
public:
    std::string out;

    void begin_object() { value_prefix(); out += '{'; first_.push_back(true); }
    void end_object() { out += '}'; first_.pop_back(); }
    void begin_array() { value_prefix(); out += '['; first_.push_back(true); }
    void end_array() { out += ']'; first_.pop_back(); }

    void key(const char* k)
    {
        comma();
        quote(k);
        out += ':';
        after_key_ = true;
    }

    void str(const std::string& s) { value_prefix(); quote(s.c_str()); }
    void str(const char* s) { value_prefix(); quote(s); }
    void num(uint64_t v) { value_prefix(); out += std::to_string(v); }
    void inum(int64_t v) { value_prefix(); out += std::to_string(v); }
    void boolean(bool v) { value_prefix(); out += v ? "true" : "false"; }
    void null() { value_prefix(); out += "null"; }
    void hex32(uint32_t v)
    {
        char b[16];
        snprintf(b, sizeof(b), "\"%08x\"", v);
        value_prefix();
        out += b;
    }

    void kv(const char* k, const char* v) { key(k); str(v); }
    void kv(const char* k, const std::string& v) { key(k); str(v); }
    void kn(const char* k, uint64_t v) { key(k); num(v); }
    void ki(const char* k, int64_t v) { key(k); inum(v); }
    void kb(const char* k, bool v) { key(k); boolean(v); }

private:
    std::vector<bool> first_;
    bool after_key_ = false;

    void comma()
    {
        if (!first_.empty()) {
            if (!first_.back())
                out += ',';
            first_.back() = false;
        }
    }

    void value_prefix()
    {
        if (after_key_)
            after_key_ = false;
        else
            comma();
    }

    void quote(const char* s)
    {
        out += '"';
        for (; *s; ++s) {
            unsigned char c = static_cast<unsigned char>(*s);
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\u%04x", c);
                    out += b;
                } else {
                    out += static_cast<char>(c);
                }
            }
        }
        out += '"';
    }
};

// Reads one flat JSON object whose values are strings, numbers, true, false or null.
// Nested values are skipped. Good enough for request headers.
class JsonRequest {
public:
    bool parse(const std::string& text)
    {
        values_.clear();
        size_t i = 0;
        ws(text, i);
        if (i >= text.size() || text[i] != '{')
            return false;
        ++i;
        for (;;) {
            ws(text, i);
            if (i < text.size() && text[i] == '}')
                return true;
            std::string k;
            if (!read_string(text, i, k))
                return false;
            ws(text, i);
            if (i >= text.size() || text[i] != ':')
                return false;
            ++i;
            ws(text, i);
            std::string v;
            if (i < text.size() && text[i] == '"') {
                if (!read_string(text, i, v))
                    return false;
            } else if (i < text.size() && (text[i] == '{' || text[i] == '[')) {
                if (!skip_nested(text, i))
                    return false;
                v.clear();
            } else {
                size_t start = i;
                while (i < text.size() && text[i] != ',' && text[i] != '}' && text[i] != ' ' && text[i] != '\n' &&
                       text[i] != '\r' && text[i] != '\t')
                    ++i;
                v = text.substr(start, i - start);
            }
            values_[k] = v;
            ws(text, i);
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            if (i < text.size() && text[i] == '}')
                return true;
            return false;
        }
    }

    bool has(const char* k) const { return values_.count(k) != 0; }

    std::string get(const char* k, const char* fallback = "") const
    {
        auto it = values_.find(k);
        return it == values_.end() ? std::string(fallback) : it->second;
    }

    uint64_t get_u64(const char* k, uint64_t fallback = 0) const
    {
        auto it = values_.find(k);
        if (it == values_.end() || it->second.empty())
            return fallback;
        if (it->second == "true")
            return 1;
        if (it->second == "false" || it->second == "null")
            return 0;
        return strtoull(it->second.c_str(), nullptr, 0);
    }

private:
    std::map<std::string, std::string> values_;

    static void ws(const std::string& t, size_t& i)
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t'))
            ++i;
    }

    static bool read_string(const std::string& t, size_t& i, std::string& out)
    {
        if (i >= t.size() || t[i] != '"')
            return false;
        ++i;
        out.clear();
        while (i < t.size() && t[i] != '"') {
            if (t[i] == '\\' && i + 1 < t.size()) {
                char c = t[i + 1];
                switch (c) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                    if (i + 5 < t.size()) {
                        unsigned cp = static_cast<unsigned>(strtoul(t.substr(i + 2, 4).c_str(), nullptr, 16));
                        out += cp < 0x80 ? static_cast<char>(cp) : '?';
                        i += 4;
                    }
                    break;
                default: out += c;
                }
                i += 2;
            } else {
                out += t[i++];
            }
        }
        if (i >= t.size())
            return false;
        ++i;
        return true;
    }

    static bool skip_nested(const std::string& t, size_t& i)
    {
        int depth = 0;
        bool in_string = false;
        for (; i < t.size(); ++i) {
            char c = t[i];
            if (in_string) {
                if (c == '\\')
                    ++i;
                else if (c == '"')
                    in_string = false;
                continue;
            }
            if (c == '"')
                in_string = true;
            else if (c == '{' || c == '[')
                ++depth;
            else if (c == '}' || c == ']') {
                if (--depth == 0) {
                    ++i;
                    return true;
                }
            }
        }
        return false;
    }
};

} // namespace efvk
