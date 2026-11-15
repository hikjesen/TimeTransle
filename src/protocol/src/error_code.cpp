#include "timetransle/protocol/error_code.hpp"

namespace timetransle::protocol {

std::string_view to_string(const ErrorCode error_code) noexcept {
    switch (error_code) {
        case ErrorCode::ok:
            return "OK";
        case ErrorCode::invalid_message:
            return "INVALID_MESSAGE";
        case ErrorCode::invalid_speed:
            return "INVALID_SPEED";
        case ErrorCode::payload_too_large:
            return "PAYLOAD_TOO_LARGE";
        case ErrorCode::unsupported_protocol:
            return "UNSUPPORTED_PROTOCOL";
        case ErrorCode::authentication_failed:
            return "AUTHENTICATION_FAILED";
        case ErrorCode::session_mismatch:
            return "SESSION_MISMATCH";
        case ErrorCode::target_generation_mismatch:
            return "TARGET_GENERATION_MISMATCH";
        case ErrorCode::stale_sequence:
            return "STALE_SEQUENCE";
        case ErrorCode::timeout:
            return "TIMEOUT";
        case ErrorCode::pipe_disconnected:
            return "PIPE_DISCONNECTED";
        case ErrorCode::target_not_found:
            return "TARGET_NOT_FOUND";
        case ErrorCode::target_identity_changed:
            return "TARGET_IDENTITY_CHANGED";
        case ErrorCode::target_exited:
            return "TARGET_EXITED";
        case ErrorCode::architecture_unsupported:
            return "ARCH_UNSUPPORTED";
        case ErrorCode::access_denied:
            return "ACCESS_DENIED";
        case ErrorCode::integrity_mismatch:
            return "INTEGRITY_MISMATCH";
        case ErrorCode::protected_process:
            return "PROTECTED_PROCESS";
        case ErrorCode::critical_process:
            return "CRITICAL_PROCESS";
        case ErrorCode::appcontainer_unsupported:
            return "APPCONTAINER_UNSUPPORTED";
        case ErrorCode::mitigation_policy_unsupported:
            return "MITIGATION_POLICY_UNSUPPORTED";
        case ErrorCode::risk_module_detected:
            return "RISK_MODULE_DETECTED";
        case ErrorCode::risk_check_incomplete:
            return "RISK_CHECK_INCOMPLETE";
        case ErrorCode::elevation_failed:
            return "ELEVATION_FAILED";
        case ErrorCode::dll_not_trusted:
            return "DLL_NOT_TRUSTED";
        case ErrorCode::dll_architecture_mismatch:
            return "DLL_ARCH_MISMATCH";
        case ErrorCode::dll_build_mismatch:
            return "DLL_BUILD_MISMATCH";
        case ErrorCode::dll_load_failed:
            return "DLL_LOAD_FAILED";
        case ErrorCode::dll_initialize_failed:
            return "DLL_INITIALIZE_FAILED";
        case ErrorCode::already_attached:
            return "ALREADY_ATTACHED";
        case ErrorCode::existing_module_incompatible:
            return "EXISTING_MODULE_INCOMPATIBLE";
        case ErrorCode::hook_create_failed:
            return "HOOK_CREATE_FAILED";
        case ErrorCode::hook_enable_failed:
            return "HOOK_ENABLE_FAILED";
        case ErrorCode::hook_rollback_failed:
            return "HOOK_ROLLBACK_FAILED";
        case ErrorCode::hook_state_invalid:
            return "HOOK_STATE_INVALID";
        case ErrorCode::not_initialized:
            return "NOT_INITIALIZED";
        case ErrorCode::already_deactivated:
            return "ALREADY_DEACTIVATED";
        case ErrorCode::controller_restarted:
            return "CONTROLLER_RESTARTED";
        case ErrorCode::shutdown_incomplete:
            return "SHUTDOWN_INCOMPLETE";
        case ErrorCode::checkpoint_unavailable:
            return "CHECKPOINT_UNAVAILABLE";
        case ErrorCode::checkpoint_empty:
            return "CHECKPOINT_EMPTY";
        case ErrorCode::checkpoint_busy:
            return "CHECKPOINT_BUSY";
        case ErrorCode::checkpoint_capture_failed:
            return "CHECKPOINT_CAPTURE_FAILED";
        case ErrorCode::checkpoint_restore_failed:
            return "CHECKPOINT_RESTORE_FAILED";
        case ErrorCode::checkpoint_allocation_outside_arena:
            return "CHECKPOINT_ALLOCATION_OUTSIDE_ARENA";
        case ErrorCode::checkpoint_device_reset:
            return "CHECKPOINT_DEVICE_RESET";

        case ErrorCode::internal_error:
            return "INTERNAL_ERROR";
    }

    return "INTERNAL_ERROR";
}

}  // namespace timetransle::protocol
