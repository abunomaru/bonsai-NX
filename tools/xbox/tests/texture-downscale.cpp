// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later

// The checks must run in any configuration: an NDEBUG build would otherwise pass vacuously.
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <iostream>
#include "video_core/renderer_d3d12/d3d12_texture_downscale.h"

namespace {
using namespace D3D12;

constexpr std::uint64_t MiB = 1024 * 1024;

TextureDownscale::Shape Bc3(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                            std::uint32_t layers = 1) {
    return {.width = width, .height = height, .levels = levels, .layers = layers,
            .block_width = 4, .block_height = 4, .bytes_per_block = 16, .multisampled = false};
}

void TestFootprint() {
    // A 1024x1024 BC3 level is 256x256 blocks of 16 bytes: 1 MiB. The chain adds a third.
    const auto bc3 = Bc3(1024, 1024, 11);
    assert(TextureDownscale::LevelBytes(bc3, 0) == MiB);
    assert(TextureDownscale::Footprint(bc3, 0) ==
           MiB + MiB / 4 + MiB / 16 + MiB / 64 + MiB / 256 + 1024 + 256 + 64 + 16 + 16 + 16);
    // Uncompressed RGBA8: four bytes a texel, no block rounding below 1:1.
    TextureDownscale::Shape rgba{.width = 300, .height = 200, .levels = 1, .layers = 1,
                                 .block_width = 1, .block_height = 1, .bytes_per_block = 4};
    assert(TextureDownscale::Footprint(rgba) == 300u * 200u * 4u);
    // Layers multiply the whole chain.
    assert(TextureDownscale::Footprint(Bc3(512, 512, 1, 6)) == 6 * (128 * 128 * 16));
}

void TestPlan() {
    using namespace TextureDownscale;
    // Disabled: 0 bytes allowed is the "no budget" sentinel, not "fit in nothing".
    assert(PlanFor(Bc3(2048, 2048, 12), 2, 0).dropped_levels == 0);
    // No option: max_drop 0 keeps everything.
    assert(PlanFor(Bc3(2048, 2048, 12), 0, MiB).dropped_levels == 0);
    // A single level cannot be dropped: the guest's only level is the texture.
    assert(PlanFor(Bc3(2048, 2048, 1), 2, MiB).dropped_levels == 0);
    // MSAA is never served down: its levels are render targets.
    auto msaa = Bc3(2048, 2048, 12);
    msaa.multisampled = true;
    assert(PlanFor(msaa, 2, MiB).dropped_levels == 0);
    // Under the allowance nothing is dropped.
    assert(PlanFor(Bc3(2048, 2048, 12), 2, 64 * MiB).dropped_levels == 0);

    // Over it, one level: 2048x2048 BC3 is 4 MiB at level 0, so the chain from level 1 is a third.
    const auto one = PlanFor(Bc3(2048, 2048, 12), 1, 4 * MiB + MiB / 2);
    assert(one.dropped_levels == 1 && one.width == 1024 && one.height == 1024 && one.levels == 11);
    assert(one.bytes == Footprint(Bc3(2048, 2048, 12), 1));

    // Two levels quarter the edge and leave a quarter of the bytes.
    const auto two = PlanFor(Bc3(2048, 2048, 12), 2, 1);
    assert(two.dropped_levels == 2 && two.width == 512 && two.height == 512 && two.levels == 10);
    assert(one.bytes == Footprint(Bc3(2048, 2048, 12), 1));
    assert(two.bytes == Footprint(Bc3(2048, 2048, 12), 2) && two.bytes < one.bytes);

    // max_drop caps it even with no room at all.
    assert(PlanFor(Bc3(2048, 2048, 12), 1, 1).dropped_levels == 1);

    // The floor stops before both axes fall under MIN_EDGE: 128 -> 64 is allowed, 64 -> 32 is not.
    const auto floored = PlanFor(Bc3(128, 128, 8), 2, 1);
    assert(floored.dropped_levels == 1 && floored.width == 64 && floored.height == 64);
    // A 64x64 texture has no level to give.
    assert(PlanFor(Bc3(64, 64, 8), 2, 1).dropped_levels == 0);

    // One wide, one short axis: the floor only stops when *both* would pass it.
    const auto wide = PlanFor(Bc3(2048, 128, 12), 2, 1);
    assert(wide.dropped_levels == 2 && wide.width == 512 && wide.height == 32);

    // Never drop the last level, even with an absurd allowance.
    const auto last = PlanFor(Bc3(2048, 2048, 2), 2, 1);
    assert(last.dropped_levels == 1 && last.levels == 1);

    // The chain is monotonic: more allowed bytes never means more dropped levels.
    for (std::uint64_t allowed = 1; allowed < 16 * MiB; allowed += MiB / 8) {
        const auto plan = PlanFor(Bc3(2048, 2048, 12), 2, allowed);
        assert(plan.bytes <= 64 * MiB);
        if (allowed >= 6 * MiB) {
            assert(plan.dropped_levels == 0);
        }
    }
}

void TestLevelMapping() {
    using namespace TextureDownscale;
    assert(HostLevel(0, 0) == 0 && GuestLevel(0, 0) == 0);
    // One level dropped: guest level 1 is the host's level 0, the largest the image still holds.
    assert(IsDropped(0, 1) && !IsDropped(1, 1));
    assert(HostLevel(1, 1) == 0 && HostLevel(11, 1) == 10);
    assert(GuestLevel(10, 1) == 11);
    // Never underflows for a dropped level (callers still check IsDropped first).
    assert(HostLevel(0, 2) == 0);
    // Round trip above the dropped range.
    for (std::uint32_t guest = 2; guest < 12; ++guest) {
        assert(GuestLevel(HostLevel(guest, 2), 2) == guest);
    }
}

} // Anonymous namespace

int main() {
    TestFootprint();
    TestPlan();
    TestLevelMapping();
    std::cout << "PASS: mip footprint, allowance, max drop, floor, last-level and level mapping\n";
}
