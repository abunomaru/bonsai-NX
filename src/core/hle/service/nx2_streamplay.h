// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ngc/guest_hid.h"
#include "core/hle/service/pulse_service.h"
#include "core/hle/service/service.h"

namespace Service {

class INx2StreamPlayUserService final : public ServiceFramework<INx2StreamPlayUserService> {
public:
    explicit INx2StreamPlayUserService(Core::System& system_)
        : ServiceFramework{system_, "nx2:stpl:u::0"}, guest_hid_memory{system_} {
        static const FunctionInfo functions[] = {
            {300, &INx2StreamPlayUserService::GetGuestHidSharedMemory, "GetGuestHidSharedMemory"},
        };
        RegisterHandlers(functions);
    }

    // Pulse: the guest HID memory's id; the memory itself is zero (no GameShare guests).
    static constexpr std::string_view PulseKindName = "nx2:stpl:u:user";
    std::string_view PulseKind() const override {
        return PulseKindName;
    }
    void PulseCapture(Pulse::HandlerCapture& capture) const override {
        capture.state.U64(guest_hid_memory.PulseId());
    }
    static SessionRequestHandlerPtr PulseRebuild(Core::System& system,
                                                 Pulse::HandlerRestore& restore) {
        auto service = std::make_shared<INx2StreamPlayUserService>(system);
        const u64 id = restore.state.U64();
        restore.state.End();
        if (R_FAILED(service->guest_hid_memory.PulseRestore(id))) {
            throw std::invalid_argument("Pulse: stpl:u guest HID memory rebuild failed");
        }
        return service;
    }

private:
    void GetGuestHidSharedMemory(HLERequestContext& ctx) {
        guest_hid_memory.Reply(ctx);
    }

    NGC::GuestHidMemory guest_hid_memory;
};

// Switch 2 stream play (GameShare), stpl:u. Command 0 (u32 0x12) opens the user service the SDK
// keeps for the process; without an object the first nn::streamplay call jumps through null.
// Firmware without stpl:u gets it from sm: as a session without a port (sm.cpp), which sm:'s
// handler rebuilds on a Pulse restore.
class INx2StreamPlayService final : public ServiceFramework<INx2StreamPlayService> {
public:
    explicit INx2StreamPlayService(Core::System& system_)
        : ServiceFramework{system_, "nx2:stpl:u"} {
        static const FunctionInfo functions[] = {
            {0, &INx2StreamPlayService::GetUserService, "GetUserService (guessed)"},
        };
        RegisterHandlers(functions);
    }

    // Pulse: stateless.
    static constexpr std::string_view PulseKindName = "nx2:stpl:u";
    std::string_view PulseKind() const override {
        return PulseKindName;
    }

private:
    void GetUserService(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) stpl:u 0 [8]={:#x}, returning a user service",
                    ctx.CommandBuffer()[8]);
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<INx2StreamPlayUserService>(ctx, system);
    }
};

} // namespace Service
