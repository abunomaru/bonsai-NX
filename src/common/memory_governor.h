// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>

// Pure policy: no clock, OS call or allocation, so every decision is covered by
// tools/xbox/tests/memory-governor.cpp.
//
// The guards around it (JIT code caches, DRAM trim, guest pool trim, the D3D12 texture cap) each
// compared the headroom with a fixed threshold once a second. A load fills memory faster than
// that: hundreds of MiB in well under a second, and on Xbox an app left over its limit is
// suspended after two seconds. The governor samples more often, follows how fast the commit is
// growing, and reports the headroom the app will have LOOKAHEAD from now if the growth goes on.
// The guards act on that instead, so they start giving memory back before the spike lands.
//
// It also turns the headroom into one pressure level for everyone, with hysteresis: a level is
// entered at once and left only after the headroom has stayed above the next level's threshold
// plus a margin for RECOVERY. The texture cap follows the level, so a game with plenty of
// headroom keeps its textures and one near the limit evicts down to a floor.
namespace Common {

class MemoryGovernor {
public:
    static constexpr std::uint64_t MiB = 1024 * 1024;

    enum class Level : std::uint8_t { Normal, Elevated, High, Critical };

    struct Thresholds {
        /// Predicted headroom at or below which each level is entered.
        std::uint64_t elevated = 1024 * MiB;
        std::uint64_t high = 512 * MiB;
        std::uint64_t critical = 256 * MiB;
        /// A level is left only above the next threshold by this fraction (1/4: 25 %).
        std::uint32_t recovery_margin_div = 4;
        /// How long the headroom must stay above that before the level drops, in microseconds.
        std::uint64_t recovery_us = 10'000'000;
        /// How far ahead the growth is projected, in microseconds.
        std::uint64_t lookahead_us = 1'500'000;
        /// Time constant of the growth average, in microseconds: a single sample does not make a
        /// trend, and a burst is followed within a few samples.
        std::uint64_t smoothing_us = 400'000;
        /// The texture cap never goes below this (0 keeps it off when the base cap is off).
        std::uint64_t min_texture_cap = 256 * MiB;
    };

    struct Decision {
        Level level;
        /// Free memory now (limit - used), 0 when over the limit.
        std::uint64_t free;
        /// The lower of free memory now and the projection LOOKAHEAD ahead: what guards compare
        /// with their thresholds.
        std::uint64_t effective_free;
        /// Commit growth in bytes per second (smoothed, never negative).
        std::uint64_t growth_per_second;
        /// Texture cap for this level, 0 when off.
        std::uint64_t texture_cap;
        /// The level differs from the previous decision.
        bool changed;
    };

    explicit MemoryGovernor(std::uint64_t base_texture_cap)
        : MemoryGovernor{base_texture_cap, Thresholds{}} {}
    MemoryGovernor(std::uint64_t base_texture_cap, Thresholds thresholds_)
        : thresholds{thresholds_}, base_cap{base_texture_cap} {}

    /// used/limit: the app's commit and its limit (limit 0: unknown). now_us: a monotonic time.
    Decision Update(std::uint64_t used, std::uint64_t limit, std::uint64_t now_us) {
        if (limit == 0) {
            // A failed measurement never implies headroom: keep the level, restart recovery.
            recovering = false;
            return Make(last_free, last_effective, false);
        }
        const std::uint64_t free = used >= limit ? 0 : limit - used;
        if (has_sample && now_us > last_time) {
            const std::uint64_t dt = now_us - last_time;
            // Growth only: memory given back is not a trend to plan for, and decays the average.
            const double sample =
                used > last_used ? static_cast<double>(used - last_used) * 1e6 / dt : 0.0;
            const double weight =
                (std::min)(1.0, static_cast<double>(dt) / thresholds.smoothing_us);
            growth = growth + (sample - growth) * weight;
        }
        has_sample = true;
        last_used = used;
        last_time = now_us;

        const double projected_spend = growth * thresholds.lookahead_us / 1e6;
        const std::uint64_t spend = projected_spend >= static_cast<double>(free)
                                        ? free
                                        : static_cast<std::uint64_t>(projected_spend);
        const std::uint64_t effective = free - spend;
        last_free = free;
        last_effective = effective;

        const Level wanted = LevelFor(effective);
        bool changed = false;
        if (wanted > level) {
            level = wanted;
            recovering = false;
            changed = true;
        } else if (wanted < level) {
            // Leave one level at a time, after the headroom stayed clear of the current level's
            // threshold, with the margin, for the whole recovery time.
            const std::uint64_t threshold = ThresholdOf(level);
            const std::uint64_t clear = threshold + threshold / thresholds.recovery_margin_div;
            if (effective <= clear) {
                recovering = false;
            } else if (!recovering) {
                recovering = true;
                healthy_since = now_us;
            } else if (now_us - healthy_since >= thresholds.recovery_us) {
                level = static_cast<Level>(static_cast<std::uint8_t>(level) - 1);
                // Each further step down needs its own recovery time.
                recovering = level > wanted;
                healthy_since = now_us;
                changed = true;
            }
        } else {
            recovering = false;
        }
        return Make(free, effective, changed);
    }

    [[nodiscard]] Level CurrentLevel() const noexcept {
        return level;
    }

    /// Texture cap of a level for a base cap (0: off, and stays off).
    [[nodiscard]] std::uint64_t TextureCapFor(Level at) const noexcept {
        if (base_cap == 0) {
            return 0;
        }
        std::uint64_t cap = base_cap;
        switch (at) {
        case Level::Normal:
            break;
        case Level::Elevated:
            cap = base_cap / 4 * 3;
            break;
        case Level::High:
            cap = base_cap / 2;
            break;
        case Level::Critical:
            cap = 0;
            break;
        }
        return (std::max)(cap, (std::min)(thresholds.min_texture_cap, base_cap));
    }

private:
    [[nodiscard]] Level LevelFor(std::uint64_t effective) const noexcept {
        if (effective <= thresholds.critical) return Level::Critical;
        if (effective <= thresholds.high) return Level::High;
        if (effective <= thresholds.elevated) return Level::Elevated;
        return Level::Normal;
    }

    [[nodiscard]] std::uint64_t ThresholdOf(Level at) const noexcept {
        switch (at) {
        case Level::Critical:
            return thresholds.critical;
        case Level::High:
            return thresholds.high;
        case Level::Elevated:
            return thresholds.elevated;
        case Level::Normal:
            break;
        }
        return 0;
    }

    Decision Make(std::uint64_t free, std::uint64_t effective, bool changed) const noexcept {
        return {level, free, effective, static_cast<std::uint64_t>(growth), TextureCapFor(level),
                changed};
    }

    Thresholds thresholds;
    std::uint64_t base_cap;
    Level level{Level::Normal};
    double growth{};
    std::uint64_t last_used{};
    std::uint64_t last_time{};
    std::uint64_t last_free{};
    std::uint64_t last_effective{};
    std::uint64_t healthy_since{};
    bool recovering{};
    bool has_sample{};
};

[[nodiscard]] constexpr const char* MemoryGovernorLevelName(MemoryGovernor::Level level) noexcept {
    switch (level) {
    case MemoryGovernor::Level::Normal:
        return "normal";
    case MemoryGovernor::Level::Elevated:
        return "elevated";
    case MemoryGovernor::Level::High:
        return "high";
    case MemoryGovernor::Level::Critical:
        return "critical";
    }
    return "?";
}

} // namespace Common
