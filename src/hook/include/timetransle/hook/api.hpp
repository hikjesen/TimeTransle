#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Windows.h>

#if defined(TIMETRANSLE_HOOK_EXPORTS)
#define TIMETRANSLE_HOOK_API __declspec(dllexport)
#else
#define TIMETRANSLE_HOOK_API __declspec(dllimport)
#endif

namespace timetransle::hook {

inline constexpr std::uint32_t local_bootstrap_magic = 0x31425454U;
inline constexpr std::uint32_t local_bootstrap_version = 1U;
inline constexpr std::uint32_t product_bootstrap_flag = 1U;
inline constexpr std::uint32_t enable_ipc_flag = 2U;
inline constexpr std::uint32_t enable_checkpoint_flag = 4U;
inline constexpr std::size_t maximum_pipe_name_characters = 128;

struct InitializeRequestV1 {
    std::uint32_t struct_size{sizeof(InitializeRequestV1)};
    std::uint32_t magic{local_bootstrap_magic};
    std::uint32_t version{local_bootstrap_version};
    std::uint32_t process_id{};
    std::uint32_t flags{product_bootstrap_flag};
    std::uint32_t controller_process_id{};
    std::uint64_t controller_creation_time{};
    std::array<std::byte, 16> session_id{};
    std::array<std::byte, 32> session_token{};
    std::array<wchar_t, maximum_pipe_name_characters> pipe_name{};
};

enum class IpcRuntimeState : std::uint32_t {
    disabled = 0,
    connecting = 1,
    active = 2,
    disconnected = 3,
    shutdown = 4,
};

struct RuntimeStateV1 {
    std::uint32_t struct_size{sizeof(RuntimeStateV1)};
    double configured_speed{1.0};
    std::uint32_t paused{};
    std::uint32_t accepting_commands{};
    std::uint64_t clock_revision{};
    std::uint32_t detours_in_flight{};
    IpcRuntimeState ipc_state{IpcRuntimeState::disabled};
    std::uint64_t last_applied_command_sequence{};
    std::uint32_t disconnect_recovery_occurred{};
};

enum class HookApiResult : std::uint32_t {
    ok = 0,
    invalid_argument = 1,
    already_initialized = 2,
    not_initialized = 3,
    inactive = 4,
    invalid_speed = 5,
    hook_setup_failed = 6,
    clock_initialization_failed = 7,
};

using InitializeFunction =
    HookApiResult(WINAPI*)(const InitializeRequestV1* request) noexcept;
using SetSpeedFunction = HookApiResult(WINAPI*)(double speed) noexcept;
using SetPausedFunction = HookApiResult(WINAPI*)(std::uint32_t paused) noexcept;
using ResetFunction = HookApiResult(WINAPI*)() noexcept;
using DeactivateFunction = HookApiResult(WINAPI*)() noexcept;
using GetStateFunction = HookApiResult(WINAPI*)(RuntimeStateV1* state) noexcept;
using GetBuildIdFunction =
    HookApiResult(WINAPI*)(char* output, std::uint32_t capacity) noexcept;
using GetApiVersionFunction = std::uint32_t(WINAPI*)() noexcept;

}  // namespace timetransle::hook

extern "C" {

TIMETRANSLE_HOOK_API timetransle::hook::HookApiResult WINAPI
TimeTransleInitialize(
    const timetransle::hook::InitializeRequestV1* request) noexcept;

// Exact CreateRemoteThread-compatible bootstrap thunk. The typed initialize
// API remains available for in-process tests and normal callers.
TIMETRANSLE_HOOK_API DWORD WINAPI
TimeTransleInitializeThread(void* request) noexcept;

TIMETRANSLE_HOOK_API timetransle::hook::HookApiResult WINAPI
TimeTransleSetSpeed(double speed) noexcept;

TIMETRANSLE_HOOK_API timetransle::hook::HookApiResult WINAPI
TimeTransleSetPaused(std::uint32_t paused) noexcept;

TIMETRANSLE_HOOK_API timetransle::hook::HookApiResult WINAPI
TimeTransleReset() noexcept;

TIMETRANSLE_HOOK_API timetransle::hook::HookApiResult WINAPI
TimeTransleDeactivate() noexcept;

TIMETRANSLE_HOOK_API timetransle::hook::HookApiResult WINAPI
TimeTransleGetState(timetransle::hook::RuntimeStateV1* state) noexcept;

TIMETRANSLE_HOOK_API timetransle::hook::HookApiResult WINAPI
TimeTransleGetBuildId(char* output, std::uint32_t capacity) noexcept;

TIMETRANSLE_HOOK_API std::uint32_t WINAPI
TimeTransleGetApiVersion() noexcept;

}

#undef TIMETRANSLE_HOOK_API
