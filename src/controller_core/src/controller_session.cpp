#include "timetransle/controller/controller_session.hpp"

#include <cmath>

namespace timetransle::controller {

CommandAcceptance ControllerSession::accept(const UiCommand& command) {
    if (snapshot_.ui_epoch != command.ui_epoch) {
        snapshot_.ui_epoch = command.ui_epoch;
        snapshot_.last_ui_sequence = 0;
    }
    if (command.ui_sequence <= snapshot_.last_ui_sequence) {
        return CommandAcceptance::stale_ui_sequence;
    }

    if (command.type == UiCommandType::select_target) {
        snapshot_.target = safety::ProcessIdentity{
            .process_id = command.process_id,
            .creation_time = command.creation_time,
            .machine = safety::MachineType::unknown,
        };
        snapshot_.target_generation = command.target_generation;
        snapshot_.applied.reset();
        speed_queue_.reset();
        snapshot_.status = SessionStatus::target_selected;
        snapshot_.last_ui_sequence = command.ui_sequence;
        return CommandAcceptance::accepted;
    }

    if (command_requires_target(command.type)) {
        if (!snapshot_.target.has_value()) {
            return CommandAcceptance::invalid_state;
        }
        if (command.target_generation != snapshot_.target_generation) {
            return CommandAcceptance::wrong_target_generation;
        }
    }

    if (command.type == UiCommandType::attach_target &&
        snapshot_.status != SessionStatus::target_ready) {
        return CommandAcceptance::invalid_state;
    }
    if (command.type == UiCommandType::relaunch_elevated &&
        snapshot_.status != SessionStatus::elevation_required) {
        return CommandAcceptance::invalid_state;
    }

    const auto requires_synced_hook =
        command.type == UiCommandType::set_speed ||
        command.type == UiCommandType::pause ||
        command.type == UiCommandType::resume ||
        command.type == UiCommandType::reset ||
        command.type == UiCommandType::save_checkpoint ||
        command.type == UiCommandType::load_checkpoint ||
        command.type == UiCommandType::deactivate;
    if (requires_synced_hook &&
        snapshot_.status != SessionStatus::hooked_synced) {
        return CommandAcceptance::invalid_state;
    }

    snapshot_.last_ui_sequence = command.ui_sequence;
    return CommandAcceptance::accepted;
}

void ControllerSession::mark_attaching() noexcept {
    snapshot_.status = SessionStatus::attaching;
}

void ControllerSession::mark_target_checking() noexcept {
    snapshot_.status = SessionStatus::target_checking;
}

void ControllerSession::mark_target_ready() noexcept {
    snapshot_.status = SessionStatus::target_ready;
}

void ControllerSession::mark_elevation_required() noexcept {
    snapshot_.status = SessionStatus::elevation_required;
}

void ControllerSession::mark_hook_syncing() noexcept {
    snapshot_.status = SessionStatus::hooked_syncing;
}

void ControllerSession::mark_hook_unknown() noexcept {
    snapshot_.status = SessionStatus::hook_unknown;
    snapshot_.applied.reset();
    speed_queue_.reset();
}

void ControllerSession::mark_target_exited() noexcept {
    snapshot_.status = SessionStatus::target_exited;
    snapshot_.applied.reset();
    snapshot_.target.reset();
    speed_queue_.reset();
}

void ControllerSession::mark_webview_failed() noexcept {
    snapshot_.status = SessionStatus::webview_failed;
}

void ControllerSession::mark_error() noexcept {
    snapshot_.status = SessionStatus::error;
}

bool ControllerSession::accept_hook_ack(
    const std::string_view target_generation,
    const std::uint64_t command_sequence,
    const std::uint64_t clock_revision,
    const double configured_speed,
    const bool paused,
    const protocol::TargetRuntime target_runtime,
    const protocol::TimeBackend time_backend,
    const protocol::EffectState effect_state) noexcept {
    if (!snapshot_.target.has_value() ||
        (snapshot_.status != SessionStatus::hooked_syncing &&
         snapshot_.status != SessionStatus::hooked_synced &&
         snapshot_.status != SessionStatus::hook_unknown) ||
        target_generation != snapshot_.target_generation ||
        !std::isfinite(configured_speed) ||
        configured_speed < 0.05 ||
        configured_speed > 8.0 ||
        (snapshot_.applied.has_value() &&
         (command_sequence < snapshot_.applied->command_sequence ||
          clock_revision < snapshot_.applied->clock_revision))) {
        return false;
    }

    snapshot_.applied = AppliedState{
        .speed = configured_speed,
        .paused = paused,
        .command_sequence = command_sequence,
        .clock_revision = clock_revision,
        .target_runtime = target_runtime,
        .time_backend = time_backend,
        .effect_state = effect_state,
    };
    snapshot_.status = SessionStatus::hooked_synced;
    return true;
}

bool ControllerSession::queue_speed(
    const double speed,
    const std::uint64_t ui_sequence) noexcept {
    return speed_queue_.submit({
        .speed = speed,
        .ui_sequence = ui_sequence,
    });
}

std::optional<ipc::SpeedCommand> ControllerSession::acknowledge_speed(
    const std::uint64_t ui_sequence) noexcept {
    return speed_queue_.acknowledge(ui_sequence);
}

const SessionSnapshot& ControllerSession::snapshot() const noexcept {
    return snapshot_;
}

bool ControllerSession::command_requires_target(
    const UiCommandType type) const noexcept {
    return type != UiCommandType::launch_program &&
           type != UiCommandType::list_processes &&
           type != UiCommandType::get_status &&
           type != UiCommandType::open_diagnostics;
}

}  // namespace timetransle::controller
