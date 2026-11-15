#include "timetransle/safety/windows_inspector.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace timetransle::safety {
namespace {

class UniqueHandle final {
public:
    explicit UniqueHandle(const HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~UniqueHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

private:
    HANDLE handle_{};
};

[[nodiscard]] constexpr std::uint64_t file_time_to_uint64(
    const FILETIME value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           value.dwLowDateTime;
}

[[nodiscard]] bool query_creation_time(
    const HANDLE process,
    std::uint64_t& creation_time) noexcept {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(process, &creation, &exit, &kernel, &user) == FALSE) {
        return false;
    }
    creation_time = file_time_to_uint64(creation);
    return true;
}

[[nodiscard]] MachineType map_machine(const USHORT machine) noexcept {
    switch (machine) {
        case IMAGE_FILE_MACHINE_I386:
            return MachineType::x86;
        case IMAGE_FILE_MACHINE_AMD64:
            return MachineType::x64;
        case IMAGE_FILE_MACHINE_ARM64:
            return MachineType::arm64;
        default:
            return MachineType::unknown;
    }
}

[[nodiscard]] bool query_machine(
    const HANDLE process,
    MachineType& machine) noexcept {
    USHORT process_machine{};
    USHORT native_machine{};
    if (IsWow64Process2(process, &process_machine, &native_machine) == FALSE) {
        return false;
    }

    machine = map_machine(
        process_machine == IMAGE_FILE_MACHINE_UNKNOWN
            ? native_machine
            : process_machine);
    return machine != MachineType::unknown;
}

[[nodiscard]] bool query_integrity_rid(
    const HANDLE process,
    DWORD& output) {
    HANDLE raw_token{};
    if (OpenProcessToken(process, TOKEN_QUERY, &raw_token) == FALSE) {
        return false;
    }
    UniqueHandle token{raw_token};

    DWORD required{};
    GetTokenInformation(token.get(), TokenIntegrityLevel, nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0) {
        return false;
    }

    std::vector<std::byte> buffer(required);
    if (GetTokenInformation(
            token.get(),
            TokenIntegrityLevel,
            buffer.data(),
            required,
            &required) == FALSE) {
        return false;
    }

    const auto* const label =
        reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
    const auto sub_authority_count =
        *GetSidSubAuthorityCount(label->Label.Sid);
    if (sub_authority_count == 0) {
        return false;
    }
    output = *GetSidSubAuthority(label->Label.Sid, sub_authority_count - 1U);
    return true;
}

[[nodiscard]] bool query_protection(
    const HANDLE process,
    bool& protected_process) noexcept {
    PROCESS_PROTECTION_LEVEL_INFORMATION information{};
    if (GetProcessInformation(
            process,
            ProcessProtectionLevelInfo,
            &information,
            sizeof(information)) == FALSE) {
        return false;
    }
    protected_process = information.ProtectionLevel != PROTECTION_LEVEL_NONE;
    return true;
}

}  // namespace

std::vector<ProcessCatalogEntry> enumerate_processes() {
    std::vector<ProcessCatalogEntry> entries;
    UniqueHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        return entries;
    }

    PROCESSENTRY32W process_entry{.dwSize = sizeof(PROCESSENTRY32W)};
    if (Process32FirstW(snapshot.get(), &process_entry) == FALSE) {
        return entries;
    }

    do {
        ProcessCatalogEntry entry;
        entry.identity.process_id = process_entry.th32ProcessID;
        entry.image_name = process_entry.szExeFile;

        UniqueHandle process{OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            process_entry.th32ProcessID)};
        if (process.get() != nullptr) {
            entry.identity_complete =
                query_creation_time(
                    process.get(),
                    entry.identity.creation_time) &&
                query_machine(process.get(), entry.identity.machine);
            WCHAR path_buffer[MAX_PATH]{};
            DWORD path_length = MAX_PATH;
            if (QueryFullProcessImageNameW(
                    process.get(),
                    0,
                    path_buffer,
                    &path_length) != FALSE &&
                path_length > 0) {
                entry.image_path.assign(path_buffer, path_length);
            }
        }
        entries.push_back(std::move(entry));
    } while (Process32NextW(snapshot.get(), &process_entry) != FALSE);

    return entries;
}

InspectionResult inspect_target(
    const std::uint32_t process_id,
    const std::uint64_t expected_creation_time,
    const MachineType injector_machine,
    const bool dll_identity_trusted) {
    InspectionResult result;
    SafetyEvidence evidence{.dll_identity_trusted = dll_identity_trusted};

    UniqueHandle process{OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        process_id)};
    evidence.query_complete = process.get() != nullptr;
    if (!evidence.query_complete) {
        result.decision = evaluate(evidence);
        return result;
    }

    result.observed_identity.process_id = process_id;
    evidence.query_complete =
        query_creation_time(
            process.get(),
            result.observed_identity.creation_time) &&
        query_machine(process.get(), result.observed_identity.machine);
    evidence.identity_matches =
        evidence.query_complete &&
        result.observed_identity.creation_time == expected_creation_time;
    evidence.architecture_supported =
        result.observed_identity.machine == MachineType::x86 ||
        result.observed_identity.machine == MachineType::x64;
    evidence.injector_architecture_matches =
        result.observed_identity.machine == injector_machine;

    DWORD current_integrity{};
    DWORD target_integrity{};
    evidence.integrity_complete =
        query_integrity_rid(GetCurrentProcess(), current_integrity) &&
        query_integrity_rid(process.get(), target_integrity);
    evidence.target_integrity_not_higher =
        evidence.integrity_complete && target_integrity <= current_integrity;

    evidence.protection_check_complete =
        query_protection(process.get(), evidence.protected_process);
    BOOL critical{};
    evidence.critical_check_complete =
        IsProcessCritical(process.get(), &critical) != FALSE;
    evidence.critical_process = critical != FALSE;

    std::uint64_t final_creation_time{};
    evidence.identity_matches =
        evidence.identity_matches &&
        query_creation_time(process.get(), final_creation_time) &&
        final_creation_time == expected_creation_time;

    result.decision = evaluate(evidence);
    return result;
}

}  // namespace timetransle::safety
