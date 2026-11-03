// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>

// Pure policy for serving a sampled texture's mip chain one or more levels down. No device, no
// allocation and no clock, so every decision is covered by tools/xbox/tests/texture-downscale.cpp.
//
// A sampled texture is read through normalized coordinates and an SRV whose MostDetailedMip names
// its first level. Dropping the largest levels of the *host* copy and starting the SRV at level
// `dropped` costs sharpness and nothing else: wrap, filtering and the mip the hardware picks for a
// derivative all stay right, and no texel has to be resampled. This is the cheap answer to "lower
// the resolution of the largest textures": the mip chain already holds those levels.
//
// The guest keeps its own full-size copy in emulated memory, so a dropped level can always be
// uploaded again if the option is turned off or the image stops qualifying: the renderer then
// recreates the image at full size (see d3d12_texture_cache.cpp). Nothing here is used for render
// targets, depth images, MSAA or single-level images.
namespace D3D12::TextureDownscale {

/// Do not serve a level below this edge: a 32x32 texture is already as cheap as it gets.
inline constexpr std::uint32_t MIN_EDGE = 64;

/// The most one texture may take before it is served down: the heavy loads keep ~1000 MiB of
/// textures, so 32 MiB leaves the worst offenders behind and the rest at full size.
inline constexpr std::uint64_t ALLOWANCE_BYTES = 32 * 1024 * 1024;

struct Shape {
    std::uint32_t width{1};
    std::uint32_t height{1};
    std::uint32_t levels{1};
    std::uint32_t layers{1};
    std::uint32_t block_width{1};  ///< texels per block (4 for BC, 1 otherwise)
    std::uint32_t block_height{1};
    std::uint32_t bytes_per_block{4};
    bool multisampled{false};
};

[[nodiscard]] constexpr std::uint32_t MipEdge(std::uint32_t edge, std::uint32_t level) {
    return (std::max)(1U, edge >> level);
}

/// Bytes of one mip level: whole blocks, every layer. The guest may pad the physical blocks, but
/// the host allocates the padded resource separately, so this is the resident size used to choose.
[[nodiscard]] constexpr std::uint64_t LevelBytes(const Shape& shape, std::uint32_t level) {
    const std::uint64_t block_w = (std::max)(1U, shape.block_width);
    const std::uint64_t block_h = (std::max)(1U, shape.block_height);
    const std::uint64_t width = MipEdge(shape.width, level);
    const std::uint64_t height = MipEdge(shape.height, level);
    const std::uint64_t blocks = ((width + block_w - 1) / block_w) * ((height + block_h - 1) / block_h);
    return blocks * shape.bytes_per_block * (std::max)(1U, shape.layers);
}

/// Bytes of the mip chain from `first_level` (inclusive) to the last one.
[[nodiscard]] constexpr std::uint64_t Footprint(const Shape& shape, std::uint32_t first_level = 0) {
    std::uint64_t total = 0;
    for (std::uint32_t level = first_level; level < shape.levels; ++level) {
        total += LevelBytes(shape, level);
    }
    return total;
}

struct Plan {
    std::uint32_t dropped_levels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t levels{};
    std::uint64_t bytes{};
};

/// Chooses how many top levels to omit so the image fits in `allowed_bytes`.
///
/// `allowed_bytes` is the most this one texture may take, and 0 disables the feature. `max_drop`
/// bounds it (1 or 2 in the boot option), and the chain always keeps at least one level. Nothing is
/// dropped when it would save nothing (small image, one level, MSAA) or when the next level would
/// go below MIN_EDGE in both axes.
[[nodiscard]] constexpr Plan PlanFor(const Shape& shape, std::uint32_t max_drop,
                                     std::uint64_t allowed_bytes) {
    Plan plan{.dropped_levels = 0, .width = shape.width, .height = shape.height,
              .levels = shape.levels, .bytes = Footprint(shape)};
    if (allowed_bytes == 0 || max_drop == 0 || shape.levels <= 1 || shape.multisampled) {
        return plan;
    }
    while (plan.dropped_levels < max_drop && plan.levels > 1 && plan.bytes > allowed_bytes) {
        const std::uint32_t next = plan.dropped_levels + 1;
        const std::uint32_t width = MipEdge(shape.width, next);
        const std::uint32_t height = MipEdge(shape.height, next);
        // Below the floor in both axes there is nothing worth keeping: stop, never go to 1x1.
        if (width < MIN_EDGE && height < MIN_EDGE) {
            break;
        }
        plan.dropped_levels = next;
        plan.width = width;
        plan.height = height;
        plan.levels = shape.levels - next;
        plan.bytes = Footprint(shape, next);
    }
    return plan;
}

/// Whether a guest level lives only in emulated memory (its host bytes were never uploaded).
[[nodiscard]] constexpr bool IsDropped(std::uint32_t guest_level, std::uint32_t dropped_levels) {
    return guest_level < dropped_levels;
}

/// The host subresource a guest level maps to once `dropped_levels` are omitted. Callers must
/// check IsDropped() first: a dropped level has no host subresource.
[[nodiscard]] constexpr std::uint32_t HostLevel(std::uint32_t guest_level,
                                                std::uint32_t dropped_levels) {
    return guest_level >= dropped_levels ? guest_level - dropped_levels : 0;
}

/// The guest level a host subresource serves; the inverse of HostLevel().
[[nodiscard]] constexpr std::uint32_t GuestLevel(std::uint32_t host_level,
                                                 std::uint32_t dropped_levels) {
    return host_level + dropped_levels;
}

} // namespace D3D12::TextureDownscale
