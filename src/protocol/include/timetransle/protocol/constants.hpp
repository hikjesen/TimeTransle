#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace timetransle::protocol {

inline constexpr std::array<std::byte, 4> frame_magic{
    std::byte{'T'},
    std::byte{'T'},
    std::byte{'L'},
    std::byte{'E'},
};

inline constexpr std::uint16_t protocol_major{1};
inline constexpr std::uint16_t protocol_minor{2};
inline constexpr std::uint16_t frame_header_bytes{64};
inline constexpr std::uint32_t max_payload_bytes{64U * 1024U};
inline constexpr std::uint32_t known_frame_flags{0};

inline constexpr double minimum_speed{0.05};
inline constexpr double initial_speed{1.0};
inline constexpr double maximum_speed{8.0};

enum class MessageType : std::uint16_t {
    hello = 1,
    hello_ack = 2,
    get_status = 3,
    status = 4,
    set_speed = 5,
    set_speed_ack = 6,
    pause = 7,
    pause_ack = 8,
    resume = 9,
    resume_ack = 10,
    reset = 11,
    reset_ack = 12,
    ping = 13,
    pong = 14,
    shutdown = 15,
    shutdown_ack = 16,
    error = 17,
    hook_state_changed = 18,
    save_checkpoint = 19,
    save_checkpoint_ack = 20,
    load_checkpoint = 21,
    load_checkpoint_ack = 22,
    get_checkpoint_status = 23,
    checkpoint_status = 24,
};

enum class TargetRuntime : std::uint8_t {
    generic = 0,
    unity_mono = 1,
};

enum class TimeBackend : std::uint8_t {
    win32_clock = 0,
    unity_mono = 1,
};

enum class EffectState : std::uint8_t {
    unverified = 0,
    pending = 1,
    applied = 2,
    degraded = 3,
    failed = 4,
};

}  // namespace timetransle::protocol
