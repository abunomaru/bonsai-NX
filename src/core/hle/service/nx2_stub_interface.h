// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/pulse_service.h"
#include "core/hle/service/service.h"

namespace Service {

// Prefix of placeholder interfaces for Switch 2-only IPC. ReportUnimplementedFunction answers
// every call on such an interface with an empty success, so boot can go on while the log shows
// which commands are used.
inline constexpr std::string_view Nx2StubPrefix = "nx2:";

class INx2StubInterface final : public ServiceFramework<INx2StubInterface> {
public:
    explicit INx2StubInterface(Core::System& system_, const char* name)
        : ServiceFramework{system_, name} {}

    // Pulse: nothing but its name (every command is an empty success). Any service's server can
    // rebuild one (pulse_checkpoint.cpp), since the root that made it may not know the kind.
    static constexpr std::string_view PulseKindName = "nx2:stub";
    std::string_view PulseKind() const override {
        return PulseKindName;
    }
    void PulseCapture(Pulse::HandlerCapture& capture) const override {
        capture.state.String(PulseName());
    }
    static SessionRequestHandlerPtr PulseRebuild(Core::System& system,
                                                 Pulse::HandlerRestore& restore) {
        const std::string name = restore.state.String();
        restore.state.End();
        if (!name.starts_with(Nx2StubPrefix)) {
            throw std::invalid_argument("Pulse: malformed Switch 2 stub interface");
        }
        return std::make_shared<INx2StubInterface>(system, InternName(name));
    }

    // Names must outlive the interface, so they are interned here.
    static const char* InternName(std::string name) {
        static std::mutex mutex;
        static std::deque<std::string> names;
        std::scoped_lock lk{mutex};
        for (const auto& n : names) {
            if (n == name) {
                return n.c_str();
            }
        }
        return names.emplace_back(std::move(name)).c_str();
    }
};

// Handler for an unknown Switch 2 command that returns an interface: hands back a stub named
// after the owning service and the command id.
inline void ReturnNx2StubInterface(Core::System& system, HLERequestContext& ctx,
                                   std::string_view owner) {
    const char* name =
        INx2StubInterface::InternName(fmt::format("{}{}::{}", Nx2StubPrefix, owner, ctx.GetCommand()));
    LOG_WARNING(Service, "(STUBBED) Switch 2 {}::{} -> stub interface {}", owner, ctx.GetCommand(),
                name);
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INx2StubInterface>(ctx, system, name);
}

} // namespace Service
