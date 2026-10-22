#include "../src/DetailCopy.hpp"

#include <cstdio>
#include <string>

using namespace layout;

static int fails = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);     \
            ++fails;                                                     \
        }                                                                \
    } while (0)

static bool has(std::string const& s, std::string const& sub) { return s.find(sub) != std::string::npos; }

static int count(std::string const& s, std::string const& sub) {
    int n = 0;
    for (size_t at = s.find(sub); at != std::string::npos; at = s.find(sub, at + 1)) ++n;
    return n;
}

int main() {
    // 1 = block, 1764 = deco (channel 1 by default), 2065 = custom particles,
    // 899 = colour trigger, 1346 = rotate, 1007 = alpha, 2905 = shader trigger,
    // 31 = start pos (typed deco).
    Classifier cls = [](int id) {
        ObjectInfo i;
        i.known = true;
        switch (id) {
            case 1: i.solid = true; i.mainChannel = 1004; break;
            case 1764: case 31: i.decoration = true; i.mainChannel = 1; break;
            case 2065: i.decoration = true; i.particle = true; i.mainChannel = 1; break;
            case 899: i.trigger = true; i.colorTrigger = true; break;
            case 1346: case 1007: case 2905: i.trigger = true; break;
        }
        return i;
    };

    std::string level =
        "kS38,1_255_2_255_3_255_6_5_7_0|1_255_2_255_3_255_6_6_5_1_7_1|1_255_2_255_3_255_6_7_7_0|,kA13,0;"
        "1,1,2,15,3,15;"                              // block: stays, glow and particles off
        "1,1764,2,20,3,15,103,1;"                     // already High Detail
        "1,1764,2,25,3,15,135,1;"                     // Hide on
        "1,1764,2,30,3,15,21,5;"                      // channel 5: opacity 0, never changed
        "1,1764,2,31,3,15,21,7;"                      // channel 7: opacity 0, but a colour trigger changes it
        "1,899,2,1,3,1,23,7,35,1;"
        "1,1764,2,40,3,15,21,8;"                      // stacked duplicate pair on channel 8
        "1,1764,2,40,3,15,21,8,20,3;"                 //   (editor layer doesn't count)
        "1,1764,2,41,3,15,21,6;"                      // pair on blending channel 6: both stay
        "1,1764,2,41,3,15,21,6;"
        "1,1764,2,42,3,15,57,9;"                      // pair in group 9, which an alpha trigger fades: both stay
        "1,1764,2,42,3,15,57,9;"
        "1,1007,2,2,3,1,51,9,35,0.5;"
        "1,1764,2,50,3,15,32,0.05;"                   // tiny
        "1,1764,2,51,3,15,32,0.05,57,11;"             // tiny but in a group: stays
        "1,2065,2,60,3,15,145,30a-1a1a0.3a20a90;"     // custom particles
        "1,1764,2,70,3,15,57,12,135,1;"               // hidden, but a rotate centre: stays
        "1,1346,2,3,3,1,51,13,71,12;"
        "1,2905,2,4,3,1;"                             // shader trigger
        "1,31,2,0,3,15,kA2,0;";                       // start pos: untouched

    DetailOptions opts;
    DetailStats st;
    auto out = makeLowDetailString(level, cls, opts, &st);
    std::printf("%s\n\n", out.substr(out.find(';') + 1).c_str());

    CHECK(st.total == 20);
    CHECK(st.existingHighDetail == 1);
    CHECK(st.hidden == 2);
    CHECK(st.duplicates == 1);
    CHECK(st.tiny == 1);
    CHECK(st.particles == 0);
    CHECK(st.shaders == 0);
    CHECK(st.reducedParticles == 1);
    CHECK(st.protectedAnchors == 1);
    CHECK(st.lowDetail == 5);

    // Header (colours) untouched.
    CHECK(out.starts_with("kS38,1_255_2_255_3_255_6_5_7_0|"));
    // Gameplay object: only flags.
    CHECK(has(out, "1,1,2,15,3,15,96,1,507,1;"));
    // Removed.
    CHECK(!has(out, "2,20,3,15"));
    CHECK(!has(out, "2,25,3,15"));
    CHECK(!has(out, "2,30,3,15"));
    CHECK(!has(out, "2,50,3,15"));
    CHECK(count(out, "2,40,3,15") == 1);
    // Kept.
    CHECK(has(out, "2,31,3,15,21,7"));
    CHECK(count(out, "2,41,3,15") == 2);
    CHECK(count(out, "2,42,3,15") == 2);
    CHECK(has(out, "2,51,3,15"));
    CHECK(has(out, "1,1764,2,70,3,15,57,12,135,1"));
    CHECK(has(out, "1,2905,2,4,3,1;"));
    CHECK(has(out, "1,1346,2,3,3,1,51,13,71,12;"));
    CHECK(has(out, "1,31,2,0,3,15,kA2,0;"));
    // Particles halved.
    CHECK(has(out, "145,15a-1a1a0.3a10a90"));

    // Mark instead of remove: everything stays, the same five get High Detail.
    {
        DetailOptions mark;
        mark.method = DetailMethod::Mark;
        DetailStats ms;
        auto marked = makeLowDetailString(level, cls, mark, &ms);
        CHECK(ms.lowDetail == 5);
        CHECK(count(marked, ";") == 21); // header + 20 objects
        CHECK(has(marked, "1,1764,2,25,3,15,135,1,96,1,507,1,103,1;"));
        CHECK(has(marked, "1,1764,2,40,3,15,21,8,20,3,96,1,507,1,103,1;"));
        CHECK(has(marked, "1,1764,2,40,3,15,21,8,96,1,507,1;"));     // first copy stays visible
        CHECK(count(marked, "103,1") == 5);
        CHECK(!has(marked, "1,1764,2,70,3,15,57,12,135,1,96,1,507,1,103,1"));
    }

    // Shaders and particles removed; flags off.
    {
        DetailOptions o;
        o.shaders = true;
        o.particles = ParticleMode::Remove;
        o.noGlow = false;
        o.noObjectParticles = false;
        DetailStats s2;
        auto o2 = makeLowDetailString(level, cls, o, &s2);
        CHECK(!has(o2, "1,2905,"));
        CHECK(!has(o2, "1,2065,"));
        CHECK(s2.shaders == 1 && s2.particles == 1);
        CHECK(has(o2, "1,1,2,15,3,15;"));
        CHECK(!has(o2, "96,1"));
    }

    // Everything off: the level comes back unchanged.
    {
        DetailOptions none;
        none.existingHighDetail = none.hidden = none.duplicates = none.tiny = false;
        none.particles = ParticleMode::Keep;
        none.noGlow = none.noObjectParticles = false;
        auto same = makeLowDetailString(level, cls, none);
        CHECK(same == level);
    }

    // Enter effects.
    {
        DetailOptions o;
        o.noEnterEffects = true;
        auto o3 = makeLowDetailString("kA13,0;1,1,2,1,3,1;1,1764,2,1,3,1;", cls, o);
        CHECK(has(o3, "1,1,2,1,3,1,96,1,507,1,64,1;"));            // gameplay: fade only
        CHECK(has(o3, "1,1764,2,1,3,1,96,1,507,1,64,1,67,1;"));    // decoration: fade and enter
    }

    // Particle strings.
    CHECK(reduceParticles("30a-1a1a0.3a20a90") == "15a-1a1a0.3a10a90");
    CHECK(reduceParticles("1a-1a1a0.3a-1") == "1a-1a1a0.3a-1");
    CHECK(reduceParticles("7") == "4");
    CHECK(reduceParticles("") == "");

    // Bad input.
    CHECK(makeLowDetailString("", cls, opts).empty());
    CHECK(makeLowDetailString("1,1,2,3;", cls, opts).empty());

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
