#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "timetransle/protocol/error_code.hpp"
#include "timetransle/ipc/protocol.hpp"
#include "timetransle/protocol/constants.hpp"
#include "timetransle/safety/policy.hpp"

namespace timetransle::controller {

struct HookRequestIdentity {
    std::string ui_epoch;
    std::uint64_t ui_sequence{};
    std::string target_generation;
};

enum class HookCommandType {
    save_checkpoint,
    load_checkpoint,
    get_checkpoint_status,
    get_status,
    set_speed,
    pause,
    resume,
    reset,
    shutdown,
};

struct HookCommand {
    HookCommandType type{HookCommandType::get_status};
    HookRequestIdentity request;
    double speed{1.0};
    bool notify{true};
};

struct HookState {
    ipc::CheckpointPayload checkpoint;
    double configured_speed{1.0};
    double effective_speed{1.0};
    bool paused{};
    bool accepting_commands{};
    bool disconnect_recovery_occurred{};
    std::uint64_t command_sequence{};
    std::uint64_t applied_command_sequence{};
    std::uint64_t clock_revision{};
    protocol::TargetRuntime target_runtime{protocol::TargetRuntime::generic};
    protocol::TimeBackend time_backend{protocol::TimeBackend::win32_clock};
    protocol::EffectState effect_state{protocol::EffectState::unverified};
};

enum class HookEventType {
    attached,
    command_applied,
    failed,
    disconnected,
};

struct HookEvent {
    HookEventType type{HookEventType::failed};
    HookCommandType command_type{HookCommandType::get_status};
    HookRequestIdentity request;
    HookState state;
    protocol::ErrorCode error{protocol::ErrorCode::ok};
};

using HookEventCallback = std::function<void(HookEvent)>;

class HookSession final {
public:
    HookSession();
    ~HookSession();

    HookSession(const HookSession&) = delete;
    HookSession& operator=(const HookSession&) = delete;

    [[nodiscard]] bool start(
        const safety::ProcessIdentity& target,
        std::filesystem::path product_directory,
        HookRequestIdentity attach_request,
        HookEventCallback callback, bool checkpoint_mode = false);

    [[nodiscard]] bool submit(HookCommand command);

    void stop(bool graceful) noexcept;

    [[nodiscard]] bool connected() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace timetransle::controller
