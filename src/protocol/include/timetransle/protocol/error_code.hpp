#pragma once

#include <cstdint>
#include <string_view>

namespace timetransle::protocol {

enum class ErrorCode : std::uint32_t {
    ok = 0,

    invalid_message = 1000,
    invalid_speed = 1001,
    payload_too_large = 1002,
    unsupported_protocol = 1003,
    authentication_failed = 1004,
    session_mismatch = 1005,
    target_generation_mismatch = 1006,
    stale_sequence = 1007,
    timeout = 1008,
    pipe_disconnected = 1009,

    target_not_found = 2000,
    target_identity_changed = 2001,
    target_exited = 2002,
    architecture_unsupported = 2003,
    access_denied = 2004,
    integrity_mismatch = 2005,
    protected_process = 2006,
    critical_process = 2007,
    appcontainer_unsupported = 2008,
    mitigation_policy_unsupported = 2009,
    risk_module_detected = 2010,
    risk_check_incomplete = 2011,
    elevation_failed = 2012,

    dll_not_trusted = 3000,
    dll_architecture_mismatch = 3001,
    dll_build_mismatch = 3002,
    dll_load_failed = 3003,
    dll_initialize_failed = 3004,
    already_attached = 3005,
    existing_module_incompatible = 3006,
    hook_create_failed = 3007,
    hook_enable_failed = 3008,
    hook_rollback_failed = 3009,
    hook_state_invalid = 3010,

    not_initialized = 4000,
    already_deactivated = 4001,
    controller_restarted = 4002,
    shutdown_incomplete = 4003,
    checkpoint_unavailable = 4100,
    checkpoint_empty = 4101,
    checkpoint_busy = 4102,
    checkpoint_capture_failed = 4103,
    checkpoint_restore_failed = 4104,
    checkpoint_device_reset = 4105,
    checkpoint_allocation_outside_arena = 4106,
    internal_error = 4099,
};

[[nodiscard]] std::string_view to_string(ErrorCode error_code) noexcept;

}  // namespace timetransle::protocol
