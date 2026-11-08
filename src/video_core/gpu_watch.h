// SPDX-FileCopyrightText: Copyright 2026 Bonsai contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <string>

#include <fmt/format.h>

#include "common/common_types.h"

// What the GPU thread is doing, readable from any thread: when the guest waits too long for a
// fence (nx2:vi2 present), it logs this to tell an idle GPU thread (the guest never sent the work)
// from one suspended on a semaphore, stuck waiting for the host GPU, or busy elsewhere. The GPU
// thread is the only writer, so the stores are relaxed and cost about nothing.
namespace VideoCommon::GpuWatch {

enum class Activity : u32 {
    Idle,
    PendingPoll, ///< Channels suspended on a semaphore acquire, polled
    Push,        ///< Running a command list
    Tick,
    Run,
    FlushInvalidate,
    HostWait, ///< Waiting for the host GPU to reach a fence (D3D12 scheduler)
    Count,
};

inline constexpr const char* activity_names[] = {
    "idle", "polling suspended channels", "running a command list", "tick", "running a task",
    "flush/invalidate", "waiting for the host GPU",
};

struct State {
    std::atomic<u32> activity{};
    std::atomic<s64> activity_since_ms{};
    std::atomic<u64> methods{};       ///< Method calls run so far
    std::atomic<u32> last_method{};   ///< Bits 0-15 method, 16-23 subchannel
    std::atomic<u64> lists{};         ///< Command lists started
    std::atomic<u64> syncpoint_increments{}; ///< Guest syncpoint increments from the puller
    std::atomic<u32> last_syncpoint{~0U};
    std::atomic<s64> last_syncpoint_ms{};
};

inline State g_state;

inline s64 NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline void SetActivity(Activity activity) {
    auto& state = g_state;
    if (state.activity.load(std::memory_order_relaxed) != static_cast<u32>(activity)) {
        state.activity.store(static_cast<u32>(activity), std::memory_order_relaxed);
        state.activity_since_ms.store(NowMs(), std::memory_order_relaxed);
    }
}

/// Sets an activity for a scope and puts the previous one back after it.
class Scope {
public:
    explicit Scope(Activity activity)
        : previous{static_cast<Activity>(g_state.activity.load(std::memory_order_relaxed))} {
        SetActivity(activity);
    }
    ~Scope() {
        SetActivity(previous);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Activity previous;
};

inline void NoteMethod(u32 subchannel, u32 method, u32 count = 1) {
    auto& state = g_state;
    state.methods.store(state.methods.load(std::memory_order_relaxed) + count,
                        std::memory_order_relaxed);
    state.last_method.store((method & 0xffff) | ((subchannel & 0xff) << 16),
                            std::memory_order_relaxed);
}

inline void NoteList() {
    auto& state = g_state;
    state.lists.store(state.lists.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}

inline void NoteSyncpointIncrement(u32 id) {
    auto& state = g_state;
    state.syncpoint_increments.store(
        state.syncpoint_increments.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    state.last_syncpoint.store(id, std::memory_order_relaxed);
    state.last_syncpoint_ms.store(NowMs(), std::memory_order_relaxed);
}

struct Snapshot {
    Activity activity;
    s64 activity_ms;
    u64 methods;
    u32 last_method;
    u64 lists;
    u64 syncpoint_increments;
    u32 last_syncpoint;
    s64 last_syncpoint_ago_ms;
};

inline Snapshot Take() {
    const auto& state = g_state;
    const s64 now = NowMs();
    const s64 last_increment = state.last_syncpoint_ms.load(std::memory_order_relaxed);
    return Snapshot{
        .activity = static_cast<Activity>(state.activity.load(std::memory_order_relaxed)),
        .activity_ms = now - state.activity_since_ms.load(std::memory_order_relaxed),
        .methods = state.methods.load(std::memory_order_relaxed),
        .last_method = state.last_method.load(std::memory_order_relaxed),
        .lists = state.lists.load(std::memory_order_relaxed),
        .syncpoint_increments = state.syncpoint_increments.load(std::memory_order_relaxed),
        .last_syncpoint = state.last_syncpoint.load(std::memory_order_relaxed),
        .last_syncpoint_ago_ms = last_increment == 0 ? -1 : now - last_increment,
    };
}

/// One line comparing two snapshots `elapsed_ms` apart: whether the GPU thread moved meanwhile.
inline std::string Describe(const Snapshot& before, const Snapshot& after, s64 elapsed_ms) {
    const auto index = static_cast<u32>(after.activity);
    const char* name = index < static_cast<u32>(Activity::Count) ? activity_names[index] : "?";
    const u64 methods = after.methods - before.methods;
    const char* verdict =
        after.activity == Activity::Idle && methods == 0
            ? "the GPU thread is idle: the guest has not sent the work that reaches the fence"
        : after.activity == Activity::PendingPoll && methods == 0
            ? "a channel is suspended on a semaphore acquire nobody released"
        : after.activity == Activity::HostWait && methods == 0
            ? "the GPU thread is stuck waiting for the host GPU (a hung submission?)"
        : methods == 0 ? "the GPU thread is stuck inside one method"
                       : "the GPU thread is running other work";
    return fmt::format(
        "GPU thread {} for {} ms; {} methods and {} lists in the last {} ms (last method {:#x} on "
        "subchannel {}); {} syncpoint increments so far, the last on syncpoint {} {} ms ago => {}",
        name, after.activity_ms, methods, after.lists - before.lists, elapsed_ms,
        after.last_method & 0xffff, (after.last_method >> 16) & 0xff, after.syncpoint_increments,
        static_cast<s32>(after.last_syncpoint), after.last_syncpoint_ago_ms, verdict);
}

} // namespace VideoCommon::GpuWatch
