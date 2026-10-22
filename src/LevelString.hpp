#pragma once

// Helpers for reading and writing GD level strings, shared by the layout
// copy and the low detail copy. No Geode or cocos includes.

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace layout::detail {

// Calls f(key, value) for every pair of a GD "key,value,key,value" list
// (or any other separator). A trailing key without a value is passed with an
// empty value.
template <class F>
void forEachPair(std::string_view text, char sep, F&& f) {
    size_t start = 0;
    while (start < text.size()) {
        size_t comma = text.find(sep, start);
        if (comma == std::string_view::npos) comma = text.size();
        std::string_view key = text.substr(start, comma - start);
        std::string_view value;
        start = comma + 1;
        if (start <= text.size()) {
            size_t next = text.find(sep, start);
            if (next == std::string_view::npos) next = text.size();
            value = text.substr(start, next - start);
            start = next + 1;
        }
        if (!key.empty()) f(key, value);
    }
}

template <class F>
void forEachPair(std::string_view text, F&& f) {
    forEachPair(text, ',', std::forward<F>(f));
}

// A GD "key,value,key,value" list, keeping the original order.
struct KeyValues {
    std::vector<std::pair<std::string, std::string>> items;

    static KeyValues parse(std::string_view text) {
        KeyValues kv;
        forEachPair(text, [&](std::string_view k, std::string_view v) { kv.items.emplace_back(k, v); });
        return kv;
    }

    std::string const* get(std::string_view key) const {
        for (auto& [k, v] : items) {
            if (k == key) return &v;
        }
        return nullptr;
    }

    void set(std::string_view key, std::string value) {
        for (auto& [k, v] : items) {
            if (k == key) {
                v = std::move(value);
                return;
            }
        }
        items.emplace_back(std::string(key), std::move(value));
    }

    template <class Pred>
    void removeIf(Pred pred) {
        std::erase_if(items, [&](auto const& kv) { return pred(kv.first); });
    }

    std::string join() const {
        std::string out;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) out += ',';
            out += items[i].first;
            out += ',';
            out += items[i].second;
        }
        return out;
    }
};

inline int toInt(std::string_view s, int fallback = 0) {
    if (s.empty()) return fallback;
    int value = fallback;
    // GD sometimes writes ints as floats ("1.0"); from_chars stops at the dot.
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc()) return fallback;
    return value;
}

inline float toFloat(std::string_view s, float fallback = 0.f) {
    if (s.empty() || s.size() > 63) return fallback;
    // strtof rather than from_chars: older Android libc++ has no float from_chars.
    char buf[64];
    s.copy(buf, s.size());
    buf[s.size()] = 0;
    char* end = nullptr;
    float value = std::strtof(buf, &end);
    return end == buf ? fallback : value;
}

inline bool isTrue(std::string_view s) {
    return s == "1" || s == "true";
}

// Old colour keys in the header (kS1-kS37). kS38 replaces them all.
inline bool isOldColourKey(std::string const& key) {
    if (key.size() < 3 || key[0] != 'k' || key[1] != 'S') return false;
    int n = 0;
    for (size_t i = 2; i < key.size(); ++i) {
        if (key[i] < '0' || key[i] > '9') return false;
        n = n * 10 + (key[i] - '0');
    }
    return n >= 1 && n <= 37;
}

// Objects that never draw anything in a real attempt but must stay for the
// level to play the same, whatever type GD gives them.
inline bool alwaysKeep(int id) {
    switch (id) {
        case 31:   // start position
        case 1816: // collision block
        case 2063: // checkpoint
        case 3032: // keyframe
        case 3640: // collision state block
        case 3643: // toggle block
            return true;
        default:
            return false;
    }
}

// Keys that hold a group whose *position* the object uses: a centre, a
// target, a destination. Objects in those groups must stay even if they're
// decoration, or the trigger moves things to the wrong place.
// From the 2.2 property list (gd-info-explorer's schema).
inline std::span<char const* const> positionalKeys(int id) {
    static constexpr char const* k71[] = {"71"};
    static constexpr char const* k51[] = {"51"};
    static constexpr char const* move[] = {"71", "395"};                     // target pos, centre
    static constexpr char const* rotate[] = {"71", "516", "517", "518", "519"}; // centre, aim min/max x/y
    static constexpr char const* sfx[] = {"51", "71", "457"};                 // sound source / listener groups
    if (id >= 3006 && id <= 3015) return k71; // area and edit area triggers: centre
    switch (id) {
        case 901: return move;
        case 1346: return rotate;
        case 2067: // scale: centre
        case 1347: // follow: follow group
        case 3033: // keyframe animation: parent group
        case 3016: // advanced follow: follow group
        case 3661: // retarget advanced follow: follow group
        case 1914: // static camera: target
        case 3600: // end: target position
        case 3613: // UI: UI target
        case 3608: // spawn particle: position group
            return k71;
        case 747:  // linked teleport portals
        case 2902: // unlinked teleport portal
        case 3022: // teleport trigger
        case 3027: // teleport orb
        case 2062: // camera edge
        case 2905: case 2907: case 2913: case 2914: case 2915: case 2916: // shader centres
            return k51;
        case 3603: // edit SFX
        case 3605: // edit song
            return sfx;
        default:
            return {};
    }
}

// Calls f(group) for each group in a "1.2.3" list.
template <class F>
void forEachGroup(std::string_view list, F&& f) {
    size_t start = 0;
    while (start < list.size()) {
        size_t dot = list.find('.', start);
        if (dot == std::string_view::npos) dot = list.size();
        int g = toInt(list.substr(start, dot - start));
        if (g > 0) f(g);
        start = dot + 1;
    }
}

// Group IDs are stored as shorts in GD, so a flat table covers them all.
struct GroupSet {
    std::vector<uint8_t> bits = std::vector<uint8_t>(65536, 0);
    void clear() { std::fill(bits.begin(), bits.end(), 0); }
    void add(int g) {
        if (g > 0 && g < 65536) bits[g] = 1;
    }
    bool has(int g) const { return g > 0 && g < 65536 && bits[g]; }
};

// Splits a level string into its header and its objects (each without the
// ';'). Returns false if there's no "k..." header.
inline bool splitLevel(std::string_view level, std::string_view& header, std::vector<std::string_view>& objects) {
    size_t headerEnd = level.find(';');
    if (headerEnd == std::string_view::npos || headerEnd == 0 || level[0] != 'k') return false;
    header = level.substr(0, headerEnd);
    size_t start = headerEnd + 1;
    while (start < level.size()) {
        size_t end = level.find(';', start);
        if (end == std::string_view::npos) end = level.size();
        if (end > start) objects.push_back(level.substr(start, end - start));
        start = end + 1;
    }
    return true;
}

// The object's ID (key 1), or -1.
inline int objectID(std::string_view raw) {
    int id = -1;
    forEachPair(raw, [&](std::string_view k, std::string_view v) {
        if (k == "1") id = toInt(v, -1);
    });
    return id;
}

} // namespace layout::detail
