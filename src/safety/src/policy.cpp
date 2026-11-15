#include "timetransle/safety/policy.hpp"

namespace timetransle::safety {

SafetyDecision evaluate(const SafetyEvidence& evidence) noexcept {
    const auto reject = [](const protocol::ErrorCode error) {
        return SafetyDecision{.allowed = false, .error = error};
    };

    if (!evidence.query_complete) {
        return reject(protocol::ErrorCode::access_denied);
    }
    if (!evidence.identity_matches) {
        return reject(protocol::ErrorCode::target_identity_changed);
    }
    if (!evidence.architecture_supported ||
        !evidence.injector_architecture_matches) {
        return reject(protocol::ErrorCode::architecture_unsupported);
    }
    if (!evidence.integrity_complete) {
        return reject(protocol::ErrorCode::access_denied);
    }
    if (!evidence.target_integrity_not_higher) {
        return reject(protocol::ErrorCode::integrity_mismatch);
    }
    if (!evidence.protection_check_complete) {
        return reject(protocol::ErrorCode::access_denied);
    }
    if (evidence.protected_process) {
        return reject(protocol::ErrorCode::protected_process);
    }
    if (!evidence.critical_check_complete) {
        return reject(protocol::ErrorCode::access_denied);
    }
    if (evidence.critical_process) {
        return reject(protocol::ErrorCode::critical_process);
    }
    if (!evidence.dll_identity_trusted) {
        return reject(protocol::ErrorCode::dll_not_trusted);
    }

    return {
        .allowed = true,
        .error = protocol::ErrorCode::ok,
    };
}

}  // namespace timetransle::safety
