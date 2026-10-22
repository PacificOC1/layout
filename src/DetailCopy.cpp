#include "DetailCopy.hpp"

#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "LevelString.hpp"

namespace layout {
namespace {

using namespace detail;

// A colour channel's starting state, from the kS38 header string.
struct Channel {
    bool blending = false;
    float opacity = 1.f;
    int copyOf = 0;           // copies another channel (9)
    bool copyOpacity = false; // and its opacity (17)
};

// What the triggers in the level can do to each channel.
struct ChannelUse {
    bool changed = false;      // some colour trigger targets it
    bool opacityUnsafe = false; // ...and can turn on blending or change opacity
};

std::unordered_map<int, Channel> readChannels(std::string_view header) {
    std::unordered_map<int, Channel> channels;
    forEachPair(header, [&](std::string_view key, std::string_view value) {
        if (key != "kS38") return;
        size_t start = 0;
        while (start < value.size()) {
            size_t bar = value.find('|', start);
            if (bar == std::string_view::npos) bar = value.size();
            Channel c;
            int id = 0;
            forEachPair(value.substr(start, bar - start), '_', [&](std::string_view k, std::string_view v) {
                if (k == "6") id = toInt(v);
                else if (k == "5") c.blending = isTrue(v);
                else if (k == "7") c.opacity = toFloat(v, 1.f);
                else if (k == "9") c.copyOf = toInt(v);
                else if (k == "17") c.copyOpacity = isTrue(v);
            });
            if (id > 0) channels[id] = c;
            start = bar + 1;
        }
    });
    return channels;
}

bool isShaderTrigger(int id) {
    return id >= 2904 && id <= 2924;
}

bool isFadeTrigger(int id) {
    return id == 3009 || id == 3014 || id == 3020; // area fade, edit area fade, enter fade
}

enum class Low : uint8_t { No, ExistingHighDetail, Hidden, Duplicate, Tiny, Particle, Shader };

// The object with keys that only matter in the editor left out, sorted, so
// stacked copies compare equal. The flags added later are the same for two
// equal objects, so comparing before them is enough.
std::string dedupeKey(std::string_view raw) {
    auto kv = KeyValues::parse(raw);
    kv.removeIf([](std::string const& k) { return k == "20" || k == "61"; }); // editor layers
    std::sort(kv.items.begin(), kv.items.end());
    return kv.join();
}

std::string formatNumber(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

} // namespace

std::string reduceParticles(std::string const& particleData) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        size_t a = particleData.find('a', start);
        fields.push_back(particleData.substr(start, a == std::string::npos ? std::string::npos : a - start));
        if (a == std::string::npos) break;
        start = a + 1;
    }
    if (fields.size() > 0) {
        int maxParticles = toInt(fields[0], 0);
        if (maxParticles > 1) fields[0] = std::to_string((maxParticles + 1) / 2);
    }
    if (fields.size() > 4) {
        float emission = toFloat(fields[4], -1.f);
        if (emission > 0.f) fields[4] = formatNumber(emission / 2.f);
    }
    std::string out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) out += 'a';
        out += fields[i];
    }
    return out;
}

std::string makeLowDetailString(std::string const& levelString, Classifier const& classify,
                                DetailOptions const& opts, DetailStats* stats) {
    DetailStats local;
    DetailStats& st = stats ? *stats : local;
    st = {};

    std::string_view header;
    std::vector<std::string_view> objects;
    if (!splitLevel(levelString, header, objects)) return {};

    std::unordered_map<int, ObjectInfo> cache;
    auto info = [&](int id) -> ObjectInfo const& {
        auto it = cache.find(id);
        if (it == cache.end()) it = cache.emplace(id, classify ? classify(id) : ObjectInfo{}).first;
        return it->second;
    };

    // Pass 1: what the level's triggers use. Positions (anchors), colour
    // channels, and groups whose opacity can change.
    auto channels = readChannels(header);
    std::unordered_map<int, ChannelUse> uses;
    bool legacyColourTriggers = false; // pre-2.0 colour triggers: their channel is in their ID
    bool fadeTriggers = false;
    GroupSet anchors;
    GroupSet alphaGroups;
    for (auto raw : objects) {
        int id = objectID(raw);
        if (id < 0) continue;
        ObjectInfo const& in = info(id);
        auto refKeys = positionalKeys(id);
        int target = 0, targetColour = 0;
        bool blending = false, copyOpacity = false;
        float opacity = 1.f;
        forEachPair(raw, [&](std::string_view k, std::string_view v) {
            if (k == "51") target = toInt(v);
            else if (k == "23") targetColour = toInt(v);
            else if (k == "17") blending = isTrue(v);
            else if (k == "35") opacity = toFloat(v, 1.f);
            else if (k == "60") copyOpacity = isTrue(v);
            for (auto rk : refKeys) {
                if (k == rk) anchors.add(toInt(v));
            }
        });
        if (id == 899) {
            auto& use = uses[targetColour];
            use.changed = true;
            if (blending || copyOpacity || opacity < 1.f) use.opacityUnsafe = true;
        } else if (in.known && in.colorTrigger) {
            legacyColourTriggers = true;
        }
        if (id == 1007) alphaGroups.add(target);
        if (isFadeTrigger(id)) fadeTriggers = true;
    }

    auto channelOf = [](int explicitID, int fallback) { return explicitID > 0 ? explicitID : fallback; };
    auto legacyTouches = [&](int ch) { return legacyColourTriggers && (ch <= 4 || ch >= 1000); };
    // Fully transparent for the whole level.
    auto invisibleChannel = [&](int ch) {
        if (ch <= 0 || legacyTouches(ch)) return false;
        if (auto u = uses.find(ch); u != uses.end() && u->second.changed) return false;
        auto c = channels.find(ch);
        return c != channels.end() && c->second.copyOf == 0 && c->second.opacity <= 0.f;
    };
    // Fully opaque and never blended, so a second copy on top changes nothing.
    auto opaqueChannel = [&](int ch) {
        if (ch <= 0 || legacyTouches(ch)) return false;
        if (ch >= 1000 && ch != 1010 && ch != 1011) return false; // special channels: unknown defaults
        if (auto u = uses.find(ch); u != uses.end() && u->second.opacityUnsafe) return false;
        auto c = channels.find(ch);
        if (c == channels.end()) return true; // default: white, opaque, no blending
        return !c->second.blending && c->second.opacity >= 1.f && !(c->second.copyOf > 0 && c->second.copyOpacity);
    };

    // Pass 2: decide and write.
    std::string out(header);
    out += ';';
    // Dedupe key hash -> the first object with it. Only hashes are stored; a
    // match is confirmed by comparing the real keys.
    std::unordered_map<size_t, size_t> seen;
    for (size_t index = 0; index < objects.size(); ++index) {
        auto raw = objects[index];
        int id = objectID(raw);
        if (id < 0) continue;
        ++st.total;
        ObjectInfo const& in = info(id);

        bool hasGroups = false, anchored = false, alphaGroup = false, groupParent = false;
        bool highDetail = false, hide = false, enterChannel = false;
        int colour1 = 0, colour2 = 0;
        float scale = 1.f, scaleX = 0.f, scaleY = 0.f;
        bool hasScaleX = false, hasScaleY = false;
        auto groupSeen = [&](int g) {
            hasGroups = true;
            anchored = anchored || anchors.has(g);
            alphaGroup = alphaGroup || alphaGroups.has(g);
        };
        forEachPair(raw, [&](std::string_view k, std::string_view v) {
            if (k == "57") forEachGroup(v, groupSeen);
            else if (k == "33") { if (int g = toInt(v); g > 0) groupSeen(g); }
            else if (k == "21") colour1 = toInt(v);
            else if (k == "22") colour2 = toInt(v);
            else if (k == "103") highDetail = isTrue(v);
            else if (k == "135") hide = isTrue(v);
            else if (k == "343") enterChannel = toInt(v) != 0;
            else if (k == "34" || k == "279") groupParent = groupParent || isTrue(v);
            else if (k == "32") scale = toFloat(v, 1.f);
            else if (k == "128") { scaleX = toFloat(v, 1.f); hasScaleX = true; }
            else if (k == "129") { scaleY = toFloat(v, 1.f); hasScaleY = true; }
        });

        bool decoration = in.known && in.decoration && !alwaysKeep(id);
        bool protectedDeco = decoration && (anchored || groupParent);
        if (protectedDeco) ++st.protectedAnchors;
        bool editable = decoration && !protectedDeco;
        bool isParticle = in.particle || id == 2065;

        int main = channelOf(colour1, in.mainChannel);
        int second = in.detailChannel < 0 ? -1 : channelOf(colour2, in.detailChannel);

        Low low = Low::No;
        if (opts.shaders && isShaderTrigger(id)) {
            low = Low::Shader;
        } else if (editable) {
            float sx = std::abs(hasScaleX ? scaleX : scale);
            float sy = std::abs(hasScaleY ? scaleY : scale);
            if (opts.existingHighDetail && highDetail) {
                low = Low::ExistingHighDetail;
            } else if (opts.hidden && (hide || (invisibleChannel(main) && (second < 0 || invisibleChannel(second))))) {
                low = Low::Hidden;
            } else if (opts.particles == ParticleMode::Remove && isParticle) {
                low = Low::Particle;
            } else if (opts.tiny && !hasGroups && !enterChannel && std::max(sx, sy) <= 0.1f) {
                low = Low::Tiny;
            }
        }

        bool dedupe = opts.duplicates && editable && low == Low::No && !isParticle && opaqueChannel(main) &&
            (second < 0 || opaqueChannel(second)) && !alphaGroup && !(fadeTriggers && (hasGroups || enterChannel));

        if (low != Low::No && opts.method == DetailMethod::Remove) {
            ++st.lowDetail;
            switch (low) {
                case Low::ExistingHighDetail: ++st.existingHighDetail; break;
                case Low::Hidden: ++st.hidden; break;
                case Low::Particle: ++st.particles; break;
                case Low::Tiny: ++st.tiny; break;
                case Low::Shader: ++st.shaders; break;
                default: break;
            }
            continue;
        }

        // Flags on everything that stays.
        bool tweakable = in.known && !in.trigger && !alwaysKeep(id);
        bool reduce = opts.particles == ParticleMode::Reduce && id == 2065;
        bool changes = low != Low::No || dedupe || reduce ||
            (tweakable && (opts.noGlow || opts.noObjectParticles || opts.noEnterEffects));
        if (!changes) {
            out += raw;
            out += ';';
            continue;
        }

        auto obj = KeyValues::parse(raw);
        if (tweakable) {
            if (opts.noGlow) obj.set("96", "1");
            if (opts.noObjectParticles) obj.set("507", "1");
            if (opts.noEnterEffects) {
                obj.set("64", "1");
                // Enter effects move objects as they come in; only decoration,
                // in case that could ever move a hitbox.
                if (decoration) obj.set("67", "1");
            }
        }
        if (reduce) {
            if (auto data = obj.get("145")) {
                auto reduced = reduceParticles(*data);
                if (reduced != *data) {
                    obj.set("145", reduced);
                    ++st.reducedParticles;
                }
            }
        }

        if (low == Low::No && dedupe) {
            std::string key = dedupeKey(raw);
            auto [it, fresh] = seen.emplace(std::hash<std::string>{}(key), index);
            if (!fresh && dedupeKey(objects[it->second]) == key) low = Low::Duplicate;
            if (low == Low::Duplicate && opts.method == DetailMethod::Remove) {
                ++st.lowDetail;
                ++st.duplicates;
                continue;
            }
        }

        if (low != Low::No) {
            // Mark: GD's Low Detail Mode leaves it out.
            obj.set("103", "1");
            ++st.lowDetail;
            switch (low) {
                case Low::ExistingHighDetail: ++st.existingHighDetail; break;
                case Low::Hidden: ++st.hidden; break;
                case Low::Duplicate: ++st.duplicates; break;
                case Low::Particle: ++st.particles; break;
                case Low::Tiny: ++st.tiny; break;
                case Low::Shader: ++st.shaders; break;
                default: break;
            }
        }
        out += obj.join();
        out += ';';
    }
    return out;
}

} // namespace layout
