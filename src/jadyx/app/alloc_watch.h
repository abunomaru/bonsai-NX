// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Diagnostic: the call stacks of the first operator new calls of chosen sizes ("alloc_watch=" in
// boot.cfg). The heap report gives the sizes of what fills the heaps but not their owners; a size
// seen there, watched here, names the code that allocates it. The check on every allocation is one
// relaxed load while nothing is watched.
namespace Jadyx::AllocWatch {

inline constexpr std::size_t MAX_SIZES = 8;
inline constexpr std::size_t MAX_CAPTURES = 16;
inline constexpr std::size_t MAX_FRAMES = 28;

/// Automatic census of every operator new of at least this size: the caller of operator new,
/// how many blocks and bytes it asked for. The heap report gives sizes; this names the code.
inline constexpr std::size_t LARGE_BLOCK_BYTES = 1 << 20;
inline constexpr std::size_t MAX_LARGE_OWNERS = 96;

/// Sets the sizes to watch (at most MAX_SIZES); call before the allocations of interest.
void Watch(std::span<const std::size_t> sizes) noexcept;

struct Capture {
    std::size_t size;
    unsigned short frame_count;
    void* frames[MAX_FRAMES];
};

/// Captures recorded since the last call, each returned once.
std::span<const Capture> TakeNew() noexcept;

/// One line per caller of a large allocation, largest first, address relative to its module;
/// empty until one is recorded. Built at audit time only: the string is allocated there, never
/// on the allocation path.
std::string DescribeLargeOwners();

} // namespace Jadyx::AllocWatch
