#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "timetransle/safety/policy.hpp"

namespace timetransle::safety {

struct ProcessCatalogEntry {
    ProcessIdentity identity{};
    std::wstring image_name;
    std::wstring image_path;
    bool identity_complete{};
};

struct InspectionResult {
    SafetyDecision decision{};
    ProcessIdentity observed_identity{};
};

[[nodiscard]] std::vector<ProcessCatalogEntry> enumerate_processes();

[[nodiscard]] InspectionResult inspect_target(
    std::uint32_t process_id,
    std::uint64_t expected_creation_time,
    MachineType injector_machine,
    bool dll_identity_trusted);

}  // namespace timetransle::safety
