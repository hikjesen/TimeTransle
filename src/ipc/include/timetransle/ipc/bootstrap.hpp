#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Windows.h>

#include "timetransle/ipc/protocol.hpp"

namespace timetransle::ipc {

inline constexpr std::uint32_t injector_bootstrap_magic = 0x31424954U;
inline constexpr std::uint32_t injector_bootstrap_version = 1U;
inline constexpr std::uint32_t injector_bootstrap_enable_ipc = 1U;
inline constexpr std::uint32_t injector_bootstrap_checkpoint = 2U;
inline constexpr std::size_t maximum_bootstrap_pipe_name_characters = 128;

// This fixed-layout record is transferred through one anonymous pipe handle
// inherited by one short-lived Injector. Secrets never appear on the command
// line or in the WebView message boundary.
struct InjectorBootstrapV1 {
    std::uint32_t struct_size{sizeof(InjectorBootstrapV1)};
    std::uint32_t magic{injector_bootstrap_magic};
    std::uint32_t version{injector_bootstrap_version};
    std::uint32_t flags{injector_bootstrap_enable_ipc};
    std::uint32_t target_process_id{};
    std::uint32_t controller_process_id{};
    std::uint64_t target_creation_time{};
    std::uint64_t controller_creation_time{};
    SessionId session_id{};
    SessionToken session_token{};
    std::array<
        wchar_t,
        maximum_bootstrap_pipe_name_characters> pipe_name{};
};

static_assert(sizeof(wchar_t) == 2);
static_assert(sizeof(InjectorBootstrapV1) == 344);

}  // namespace timetransle::ipc
