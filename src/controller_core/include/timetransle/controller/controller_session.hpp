#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "timetransle/controller/web_message.hpp"
#include "timetransle/ipc/command_queue.hpp"
#include "timetransle/protocol/constants.hpp"
#include "timetransle/safety/policy.hpp"

namespace timetransle::controller {

enum class SessionStatus {
    idle,
    target_selected,
    target_checking,
    target_ready,
    elevation_required,
    attaching,
    hooked_syncing,
    hooked_synced,
    hook_unknown,
    target_exited,
    webview_failed,
    error,
};

enum class CommandAcceptance {
    accepted,
    stale_ui_sequence,
    wrong_target_generation,
    invalid_state,
};

struct AppliedState {
    double speed{1.0};
    bool paused{};
    std::uint64_t command_sequence{};
    std::uint64_t clock_revision{};
    protocol::TargetRuntime target_runtime{protocol::TargetRuntime::generic};
    protocol::TimeBackend time_backend{protocol::TimeBackend::win32_clock};
    protocol::EffectState effect_state{protocol::EffectState::unverified};
};

struct SessionSnapshot {
    SessionStatus status{SessionStatus::idle};
    std::string ui_epoch;
    std::uint64_t last_ui_sequence{};
    std::string target_generation;
    std::optional<safety::ProcessIdentity> target;
    std::optional<AppliedState> applied;
};

class ControllerSession final {
public:
    [[nodiscard]] CommandAcceptance accept(const UiCommand& command);

    void mark_attaching() noexcept;
    void mark_target_checking() noexcept;
    void mark_target_ready() noexcept;
    void mark_elevation_required() noexcept;
    void mark_hook_syncing() noexcept;
    void mark_hook_unknown() noexcept;
    void mark_target_exited() noexcept;
    void mark_webview_failed() noexcept;
    void mark_error() noexcept;

    [[nodiscard]] bool accept_hook_ack(
        std::string_view target_generation,
        std::uint64_t command_sequence,
        std::uint64_t clock_revision,
        double configured_speed,
        bool paused,
        protocol::TargetRuntime target_runtime,
        protocol::TimeBackend time_backend,
        protocol::EffectState effect_state) noexcept;

    [[nodiscard]] bool queue_speed(
        double speed,
        std::uint64_t ui_sequence) noexcept;
    [[nodiscard]] std::optional<ipc::SpeedCommand> acknowledge_speed(
        std::uint64_t ui_sequence) noexcept;

    [[nodiscard]] const SessionSnapshot& snapshot() const noexcept;

private:
    [[nodiscard]] bool command_requires_target(UiCommandType type) const noexcept;

    SessionSnapshot snapshot_;
    ipc::BoundedSpeedCommandQueue speed_queue_;
};

}  // namespace timetransle::controller
