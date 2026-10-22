#pragma once

// Pure logic for the low detail copy: takes a decompressed GD level string
// and returns a new one that looks almost the same but draws less.
// No Geode or cocos includes, so it can be unit-tested on any machine.
//
// Only decoration is ever removed or marked, plus shader triggers if asked.
// Gameplay objects and gameplay triggers stay exactly as they are (apart
// from the glow / particle / enter effect flags below), and decoration that
// a trigger uses the position of is never touched.

#include <string>

#include "LayoutCopy.hpp"

namespace layout {

enum class DetailMethod {
    Remove, // leave low value objects out of the copy
    Mark,   // keep them with High Detail (103) on: the copy looks like the
            // original, and GD's Low Detail Mode leaves them out
};

enum class ParticleMode { Keep, Reduce, Remove };

struct DetailOptions {
    DetailMethod method = DetailMethod::Remove;

    // What counts as low value (decoration only).
    bool existingHighDetail = true; // already marked High Detail by the creator
    bool hidden = true;             // never visible: Hide on, or every colour it uses is fully transparent for the whole level
    bool duplicates = true;         // exact copy stacked on another, on opaque channels without blending
    bool tiny = true;               // scaled to 10% or less and not in any group
    ParticleMode particles = ParticleMode::Reduce; // particle objects: Reduce halves their particle count and emission
    bool shaders = false;           // shader triggers

    // Applied to every object that stays (gameplay too; they only change looks).
    bool noGlow = true;            // No Glow (96)
    bool noObjectParticles = true; // No Particles (507): orb, pad, portal and coin particles
    bool noEnterEffects = false;   // Don't Fade (64) on everything, Don't Enter (67) on decoration
};

struct DetailStats {
    int total = 0;
    int lowDetail = 0; // removed, or marked High Detail
    int existingHighDetail = 0;
    int hidden = 0;
    int duplicates = 0;
    int tiny = 0;
    int particles = 0;
    int shaders = 0;
    int reducedParticles = 0;
    int protectedAnchors = 0; // decoration left alone because a trigger uses its position
};

std::string makeLowDetailString(std::string const& levelString, Classifier const& classify,
                                DetailOptions const& opts, DetailStats* stats = nullptr);

// Halves a particle string's max particles (field 0) and emission (field 4).
std::string reduceParticles(std::string const& particleData);

} // namespace layout
