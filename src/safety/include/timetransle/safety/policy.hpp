#pragma once

#include <cstdint>

#include "timetransle/protocol/error_code.hpp"

namespace timetransle::safety {

enum class MachineType : std::uint16_t {
    unknown = 0,
    x86 = 0x014c,
    x64 = 0x8664,
    arm64 = 0xAA64,
};

struct ProcessIdentity {
    std::uint32_t process_id{};
    std::uint64_t creation_time{};
    MachineType machine{MachineType::unknown};
};

struct SafetyEvidence {
    bool query_complete{};
    bool identity_matches{};
    bool architecture_supported{};
    bool injector_architecture_matches{};
    bool integrity_complete{};
    bool target_integrity_not_higher{};
    bool protection_check_complete{};
    bool protected_process{};
    bool critical_check_complete{};
    bool critical_process{};
    bool dll_identity_trusted{};
};

struct SafetyDecision {
    bool allowed{};
    protocol::ErrorCode error{protocol::ErrorCode::access_denied};
};

[[nodiscard]] SafetyDecision evaluate(const SafetyEvidence& evidence) noexcept;

}  // namespace timetransle::safety
