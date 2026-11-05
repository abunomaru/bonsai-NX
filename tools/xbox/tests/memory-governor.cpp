// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later

// g++ -std=c++20 -Isrc tools/xbox/tests/memory-governor.cpp -o memory-governor && ./memory-governor

// The checks must run in any configuration: an NDEBUG build would otherwise pass vacuously.
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <iostream>
#include "common/memory_governor.h"

namespace {
using Common::MemoryGovernor;
using Level = MemoryGovernor::Level;
constexpr std::uint64_t M = MemoryGovernor::MiB;
constexpr std::uint64_t limit = 5120 * M;
constexpr std::uint64_t ms = 1000; // microseconds
constexpr std::uint64_t UsedFor(std::uint64_t free) {
    return limit - free;
}

void TestLevelsWithoutGrowth() {
    MemoryGovernor governor{768 * M};
    std::uint64_t t = 1 * ms;
    auto d = governor.Update(UsedFor(2048 * M), limit, t);
    assert(d.level == Level::Normal && !d.changed && d.texture_cap == 768 * M);
    assert(d.free == 2048 * M && d.effective_free == 2048 * M);
    // Steady at each band: entered at once.
    d = governor.Update(UsedFor(2048 * M), limit, t += 100 * ms);
    d = governor.Update(UsedFor(1000 * M), limit, t += 100 * ms);
    // Growth of 1048 MiB in 100 ms projects far below: straight to critical.
    assert(d.level == Level::Critical && d.changed);
    assert(d.texture_cap == 256 * M);
}

void TestPredictsALoadSpike() {
    MemoryGovernor governor{768 * M};
    std::uint64_t t = 1 * ms;
    // 1.6 GiB free and growing ~800 MiB/s: the plain threshold says Normal, the projection
    // says the app reaches the High band within the lookahead.
    std::uint64_t free = 1600 * M;
    MemoryGovernor::Decision d{};
    for (int i = 0; i < 5; ++i) {
        d = governor.Update(UsedFor(free), limit, t);
        free -= 80 * M;
        t += 100 * ms;
    }
    assert(d.free > 1024 * M);         // still above the Elevated threshold now
    assert(d.growth_per_second > 300 * M);
    assert(d.effective_free < d.free); // but the projection is lower
    assert(d.level >= Level::Elevated);
}

void TestGrowthDecays() {
    MemoryGovernor governor{768 * M};
    std::uint64_t t = 1 * ms;
    governor.Update(UsedFor(3000 * M), limit, t);
    governor.Update(UsedFor(2500 * M), limit, t += 100 * ms);
    auto d = governor.Update(UsedFor(2500 * M), limit, t += 100 * ms);
    const auto after_burst = d.growth_per_second;
    for (int i = 0; i < 30; ++i) {
        d = governor.Update(UsedFor(2500 * M), limit, t += 100 * ms);
    }
    assert(d.growth_per_second < after_burst / 100);
    // Memory given back is never negative growth: the projection is never above the headroom,
    // and what is left of the decayed burst moves it by well under a MiB.
    d = governor.Update(UsedFor(3500 * M), limit, t += 100 * ms);
    assert(d.effective_free <= d.free && d.free - d.effective_free < 1 * M);
}

void TestRecoveryHysteresis() {
    MemoryGovernor governor{1024 * M};
    std::uint64_t t = 1 * ms;
    auto d = governor.Update(UsedFor(200 * M), limit, t);
    assert(d.level == Level::Critical && d.texture_cap == 256 * M);
    // Just above the critical threshold but inside the 25 % margin: stays critical.
    for (int i = 0; i < 200; ++i) {
        d = governor.Update(UsedFor(300 * M), limit, t += 100 * ms);
    }
    assert(d.level == Level::Critical);
    // Clear of the margin, but not for long enough yet.
    d = governor.Update(UsedFor(4000 * M), limit, t += 100 * ms);
    d = governor.Update(UsedFor(4000 * M), limit, t += 5000 * ms);
    assert(d.level == Level::Critical);
    // After the recovery time: one level at a time, each with its own wait.
    d = governor.Update(UsedFor(4000 * M), limit, t += 5100 * ms);
    assert(d.level == Level::High && d.changed && d.texture_cap == 512 * M);
    d = governor.Update(UsedFor(4000 * M), limit, t += 100 * ms);
    assert(d.level == Level::High && !d.changed);
    d = governor.Update(UsedFor(4000 * M), limit, t += 10'000 * ms);
    assert(d.level == Level::Elevated && d.texture_cap == 768 * M);
    d = governor.Update(UsedFor(4000 * M), limit, t += 10'000 * ms);
    assert(d.level == Level::Normal && d.texture_cap == 1024 * M);
    // A sudden 3 GiB drop is a spike: critical at once, whatever band it lands in.
    d = governor.Update(UsedFor(900 * M), limit, t += 100 * ms);
    assert(d.level == Level::Critical && d.changed);

    // A dip back inside the margin restarts the recovery wait.
    MemoryGovernor steady{1024 * M};
    t = 1 * ms;
    steady.Update(UsedFor(900 * M), limit, t);
    assert(steady.CurrentLevel() == Level::Elevated);
    // Growing back to 1400 MiB free gives memory back, which is no growth.
    for (int i = 0; i < 90; ++i) steady.Update(UsedFor(1400 * M), limit, t += 100 * ms);
    // A slow dip (130 MiB over a second) to 1270 MiB: inside 1024 * 1.25, so not clear.
    steady.Update(UsedFor(1270 * M), limit, t += 1000 * ms);
    for (int i = 0; i < 20; ++i) steady.Update(UsedFor(1400 * M), limit, t += 100 * ms);
    assert(steady.CurrentLevel() == Level::Elevated);
    for (int i = 0; i < 100; ++i) steady.Update(UsedFor(1400 * M), limit, t += 100 * ms);
    assert(steady.CurrentLevel() == Level::Normal);
}

void TestUnknownLimitAndOverLimit() {
    MemoryGovernor governor{768 * M};
    std::uint64_t t = 1 * ms;
    auto d = governor.Update(UsedFor(200 * M), limit, t);
    assert(d.level == Level::Critical);
    d = governor.Update(0, 0, t += 100 * ms); // failed measurement: no headroom implied
    assert(d.level == Level::Critical && !d.changed);
    d = governor.Update(limit + 10 * M, limit, t += 100 * ms);
    assert(d.free == 0 && d.effective_free == 0 && d.level == Level::Critical);
}

void TestTextureCaps() {
    MemoryGovernor off{0};
    assert(off.TextureCapFor(Level::Normal) == 0 && off.TextureCapFor(Level::Critical) == 0);
    MemoryGovernor small{128 * M};
    // Never above the configured base, even at the floor.
    assert(small.TextureCapFor(Level::Critical) == 128 * M);
    MemoryGovernor regular{768 * M};
    assert(regular.TextureCapFor(Level::Elevated) == 576 * M);
    assert(regular.TextureCapFor(Level::High) == 384 * M);
    assert(regular.TextureCapFor(Level::Critical) == 256 * M);
}
} // namespace

int main() {
    TestLevelsWithoutGrowth();
    TestPredictsALoadSpike();
    TestGrowthDecays();
    TestRecoveryHysteresis();
    TestUnknownLimitAndOverLimit();
    TestTextureCaps();
    std::cout << "memory-governor: all checks passed\n";
    return 0;
}
