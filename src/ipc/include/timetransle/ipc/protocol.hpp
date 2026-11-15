#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "timetransle/protocol/constants.hpp"
#include "timetransle/protocol/error_code.hpp"

namespace timetransle::ipc {

using SessionId = std::array<std::byte, 16>;
using SessionToken = std::array<std::byte, 32>;

inline constexpr std::size_t hello_payload_bytes = 40;
inline constexpr std::size_t state_payload_bytes = 40;
inline constexpr std::size_t error_payload_bytes = 4;

inline constexpr std::size_t checkpoint_payload_bytes = 16;
inline constexpr std::size_t checkpoint_ack_payload_bytes = state_payload_bytes + checkpoint_payload_bytes;
struct CheckpointPayload {
    bool available{};
    bool has_snapshot{};
    std::uint32_t operation{};
    protocol::ErrorCode error{protocol::ErrorCode::ok};
};
void encode_checkpoint_payload(const CheckpointPayload& value, std::span<std::byte> bytes) noexcept;
bool decode_checkpoint_payload(std::span<const std::byte> bytes, CheckpointPayload& value) noexcept;

struct FrameHeader {
    protocol::MessageType message_type{};
    std::uint32_t payload_bytes{};
    SessionId session_id{};
    std::uint64_t request_id{};
    std::uint64_t command_sequence{};
};

struct Frame {
    FrameHeader header{};
    std::array<std::byte, protocol::max_payload_bytes> payload{};
};

struct HelloPayload {
    SessionToken token{};
    std::uint32_t process_id{};
};

struct StatePayload {
    double configured_speed{1.0};
    double effective_speed{1.0};
    std::uint64_t clock_revision{};
    std::uint64_t last_applied_command_sequence{};
    bool paused{};
    bool accepting_commands{};
    bool disconnect_recovery_occurred{};
    protocol::TargetRuntime target_runtime{protocol::TargetRuntime::generic};
    protocol::TimeBackend time_backend{protocol::TimeBackend::win32_clock};
    protocol::EffectState effect_state{protocol::EffectState::unverified};
};

enum class HeaderParseResult {
    ok,
    truncated,
    invalid_magic,
    unsupported_version,
    invalid_header_size,
    unknown_message_type,
    unknown_flags,
    payload_too_large,
    reserved_not_zero,
};

[[nodiscard]] HeaderParseResult parse_header(
    std::span<const std::byte> bytes,
    FrameHeader& output) noexcept;

[[nodiscard]] std::array<std::byte, protocol::frame_header_bytes> encode_header(
    const FrameHeader& header) noexcept;

[[nodiscard]] bool sessions_equal(
    const SessionId& left,
    const SessionId& right) noexcept;

void encode_hello_payload(
    const HelloPayload& payload,
    std::span<std::byte, hello_payload_bytes> output) noexcept;

[[nodiscard]] bool decode_hello_payload(
    std::span<const std::byte> input,
    HelloPayload& output) noexcept;

void encode_state_payload(
    const StatePayload& payload,
    std::span<std::byte, state_payload_bytes> output) noexcept;

[[nodiscard]] bool decode_state_payload(
    std::span<const std::byte> input,
    StatePayload& output) noexcept;

void encode_error_payload(
    protocol::ErrorCode error,
    std::span<std::byte, error_payload_bytes> output) noexcept;

[[nodiscard]] bool decode_error_payload(
    std::span<const std::byte> input,
    protocol::ErrorCode& output) noexcept;

void encode_speed_payload(
    double speed,
    std::span<std::byte, sizeof(double)> output) noexcept;

[[nodiscard]] bool decode_speed_payload(
    std::span<const std::byte> input,
    double& output) noexcept;

}  // namespace timetransle::ipc
