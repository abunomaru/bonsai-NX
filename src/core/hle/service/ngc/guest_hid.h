// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <utility>

#include "core/core.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::NGC {

struct GuestHidMemoryConfig {
    u64 size;
    u64 offset;
};
static_assert(sizeof(GuestHidMemoryConfig) == 0x10);

inline constexpr u64 GuestHidMemorySize = 0x9b000;

namespace Detail {
// The live guest HID memories, by System and id, so that Pulse can name the one a process maps
// (bonsai-NX #16). Nothing writes them while no GameShare guest is connected: a restore makes a
// fresh, zeroed one under the captured id.
struct GuestHidRegistry {
    std::mutex mutex;
    std::map<std::pair<const Core::System*, u64>, Kernel::KSharedMemory*> memories;
    u64 next_id{1};
};
inline GuestHidRegistry& Registry() {
    static GuestHidRegistry registry;
    return registry;
}
} // namespace Detail

/// The GameShare guest HID shared memory of one service interface (stpl:u user service, ngc's
/// IUserShimScopedObject), made on its first GetGuestHidSharedMemory.
class GuestHidMemory {
public:
    explicit GuestHidMemory(Core::System& system_) : system{system_} {}
    ~GuestHidMemory() {
        if (!memory) return;
        {
            auto& registry = Detail::Registry();
            std::scoped_lock lk{registry.mutex};
            registry.memories.erase({&system, id});
        }
        memory->Close(system.Kernel());
    }
    GuestHidMemory(const GuestHidMemory&) = delete;
    GuestHidMemory& operator=(const GuestHidMemory&) = delete;

    void Reply(HLERequestContext& ctx) {
        if (const Result result = Create(0); R_FAILED(result)) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(result);
            return;
        }
        IPC::ResponseBuilder rb{ctx, 6, 1};
        rb.Push(ResultSuccess);
        rb.PushRaw(GuestHidMemoryConfig{GuestHidMemorySize, 0});
        rb.PushCopyObjects(ctx, memory);
    }

    /// Pulse: the id the process's mapping is recorded under, 0 before the first request.
    u64 PulseId() const {
        return id;
    }
    /// Pulse restore: the memory again, under the captured id (0: none was made).
    Result PulseRestore(u64 captured_id) {
        return captured_id ? Create(captured_id) : ResultSuccess;
    }

private:
    Result Create(u64 wanted_id) {
        if (memory) return ResultSuccess;
        auto& kernel = system.Kernel();
        auto* created = Kernel::KSharedMemory::Create(kernel);
        const Result result =
            created ? created->Initialize(kernel, system.DeviceMemory(), nullptr,
                                          Kernel::Svc::MemoryPermission::ReadWrite,
                                          Kernel::Svc::MemoryPermission::Read, GuestHidMemorySize)
                    : Kernel::ResultOutOfResource;
        if (R_FAILED(result)) {
            if (created) created->Close(kernel);
            return result;
        }
        Kernel::KSharedMemory::Register(kernel, created);
        memory = created;
        auto& registry = Detail::Registry();
        std::scoped_lock lk{registry.mutex};
        if (wanted_id) {
            id = wanted_id;
            registry.next_id = (std::max)(registry.next_id, wanted_id + 1);
        } else {
            id = registry.next_id++;
        }
        registry.memories[{&system, id}] = memory;
        LOG_INFO(Service_NGC,
                 "GameShare guest HID shared memory: size={:#x}, offset=0, no guests connected",
                 GuestHidMemorySize);
        return ResultSuccess;
    }

    Core::System& system;
    Kernel::KSharedMemory* memory{};
    u64 id{};
};

/// Pulse: the id of a guest HID memory of this System, if `shared` is one.
inline std::optional<u64> PulseFindGuestHidMemory(const Core::System& system,
                                                  const Kernel::KSharedMemory* shared) {
    auto& registry = Detail::Registry();
    std::scoped_lock lk{registry.mutex};
    for (const auto& [key, memory] : registry.memories) {
        if (key.first == &system && memory == shared) return key.second;
    }
    return std::nullopt;
}

/// Pulse restore: the memory an interface rebuilt under `id`, null if none did.
inline Kernel::KSharedMemory* PulseGuestHidMemory(const Core::System& system, u64 id) {
    auto& registry = Detail::Registry();
    std::scoped_lock lk{registry.mutex};
    const auto found = registry.memories.find({&system, id});
    return found != registry.memories.end() ? found->second : nullptr;
}

} // namespace Service::NGC
