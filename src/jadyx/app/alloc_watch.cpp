// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#include <intrin.h>

#include <windows.h>

#include "jadyx/app/alloc_watch.h"

namespace Jadyx::AllocWatch {
namespace {

std::array<std::atomic<std::size_t>, MAX_SIZES> g_sizes{};
std::atomic<std::size_t> g_count{};
/// Captures per watched size, so a frequent size cannot use up the slots of a rare one.
std::array<std::atomic<unsigned>, MAX_SIZES> g_per_size{};
constexpr unsigned CAPTURES_PER_SIZE = 2;

std::array<Capture, MAX_CAPTURES> g_captures{};
std::atomic<std::size_t> g_reserved{}; ///< Slots handed out.
std::array<std::atomic<bool>, MAX_CAPTURES> g_ready{};
std::size_t g_taken{}; ///< Slots already returned by TakeNew (one reader).

struct LargeSlot {
    std::atomic<std::uintptr_t> caller{0};
    std::atomic<std::uint64_t> blocks{0};
    std::atomic<std::uint64_t> bytes{0};
    std::atomic<std::uint64_t> largest{0};
};
std::array<LargeSlot, MAX_LARGE_OWNERS> g_large{};

// The census of blocks of a MiB and up, keyed by the caller of operator new. It runs inside
// operator new: no allocation, no lock, no exception, bounded probing.
void RecordLarge(std::size_t size, void* caller) noexcept {
    if (size < LARGE_BLOCK_BYTES || caller == nullptr) {
        return;
    }
    const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(caller);
    const std::size_t start = (key >> 4) % MAX_LARGE_OWNERS;
    for (std::size_t i = 0; i < MAX_LARGE_OWNERS; ++i) {
        LargeSlot& slot = g_large[(start + i) % MAX_LARGE_OWNERS];
        std::uintptr_t expected = 0;
        if (!slot.caller.compare_exchange_strong(expected, key, std::memory_order_relaxed) &&
            expected != key) {
            continue;
        }
        slot.blocks.fetch_add(1, std::memory_order_relaxed);
        slot.bytes.fetch_add(size, std::memory_order_relaxed);
        std::uint64_t largest = slot.largest.load(std::memory_order_relaxed);
        while (largest < size &&
               !slot.largest.compare_exchange_weak(largest, size, std::memory_order_relaxed)) {
        }
        return;
    }
}

// Lock-free and allocation-free: it runs inside operator new.
void Check(std::size_t size, void* caller) noexcept {
    RecordLarge(size, caller);
    const std::size_t count = g_count.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < count; ++i) {
        if (g_sizes[i].load(std::memory_order_relaxed) != size) {
            continue;
        }
        if (g_per_size[i].fetch_add(1, std::memory_order_relaxed) >= CAPTURES_PER_SIZE) {
            return;
        }
        const std::size_t slot = g_reserved.fetch_add(1, std::memory_order_relaxed);
        if (slot >= MAX_CAPTURES) {
            return;
        }
        Capture& capture = g_captures[slot];
        capture.size = size;
        capture.frame_count = RtlCaptureStackBackTrace(1, static_cast<DWORD>(MAX_FRAMES),
                                                       capture.frames, nullptr);
        g_ready[slot].store(true, std::memory_order_release);
        return;
    }
}

} // Anonymous namespace

void Watch(std::span<const std::size_t> sizes) noexcept {
    const std::size_t count = sizes.size() < MAX_SIZES ? sizes.size() : MAX_SIZES;
    for (std::size_t i = 0; i < count; ++i) {
        g_sizes[i].store(sizes[i], std::memory_order_relaxed);
    }
    g_count.store(count, std::memory_order_release);
}

std::span<const Capture> TakeNew() noexcept {
    const std::size_t begin = g_taken;
    std::size_t end = begin;
    while (end < MAX_CAPTURES && g_ready[end].load(std::memory_order_acquire)) {
        ++end;
    }
    g_taken = end;
    return {g_captures.data() + begin, end - begin};
}

std::string DescribeLargeOwners() {
    struct Row {
        std::uintptr_t caller;
        std::uint64_t blocks;
        std::uint64_t bytes;
        std::uint64_t largest;
    };
    std::array<Row, MAX_LARGE_OWNERS> rows{};
    std::size_t count = 0;
    for (const LargeSlot& slot : g_large) {
        const std::uintptr_t caller = slot.caller.load(std::memory_order_relaxed);
        if (caller == 0) {
            continue;
        }
        rows[count++] = {caller, slot.blocks.load(std::memory_order_relaxed),
                         slot.bytes.load(std::memory_order_relaxed),
                         slot.largest.load(std::memory_order_relaxed)};
    }
    if (count == 0) {
        return {};
    }
    // Largest first; the census is bounded, so a plain insertion sort is enough.
    for (std::size_t i = 1; i < count; ++i) {
        const Row row = rows[i];
        std::size_t j = i;
        while (j > 0 && rows[j - 1].bytes < row.bytes) {
            rows[j] = rows[j - 1];
            --j;
        }
        rows[j] = row;
    }
    std::string text = "large blocks (caller, module-relative):";
    for (std::size_t i = 0; i < count && i < 8; ++i) {
        std::uintptr_t rva = rows[i].caller;
        HMODULE module{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(rows[i].caller), &module) &&
            module != nullptr) {
            rva = rows[i].caller - reinterpret_cast<std::uintptr_t>(module);
        }
        char entry[72];
        std::snprintf(entry, sizeof(entry), " %llx x%llu=%lluM(max %lluM)",
                      static_cast<unsigned long long>(rva),
                      static_cast<unsigned long long>(rows[i].blocks),
                      static_cast<unsigned long long>(rows[i].bytes >> 20),
                      static_cast<unsigned long long>(rows[i].largest >> 20));
        text += entry;
    }
    return text;
}

} // namespace Jadyx::AllocWatch

// The replaceable global allocation functions, so every operator new in the executable passes the
// watch. Same behaviour as the MSVC defaults (malloc/free).
void* operator new(std::size_t size) {
    Jadyx::AllocWatch::Check(size, _ReturnAddress());
    if (void* const pointer = std::malloc(size != 0 ? size : 1)) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
    return operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    Jadyx::AllocWatch::Check(size, _ReturnAddress());
    return std::malloc(size != 0 ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return operator new(size, tag);
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}
