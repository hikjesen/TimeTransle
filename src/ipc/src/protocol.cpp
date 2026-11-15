#include "timetransle/ipc/protocol.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace timetransle::ipc {
namespace {

template <typename Integer>
[[nodiscard]] Integer read_little_endian(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    static_assert(std::is_unsigned_v<Integer>);

    Integer value{};
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        value |= static_cast<Integer>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

template <typename Integer>
void write_little_endian(
    const Integer value,
    const std::span<std::byte> bytes,
    const std::size_t offset) noexcept {
    static_assert(std::is_unsigned_v<Integer>);

    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] bool is_known_message_type(const std::uint16_t value) noexcept {
    return value >= static_cast<std::uint16_t>(protocol::MessageType::hello) &&
           value <=
               static_cast<std::uint16_t>(protocol::MessageType::checkpoint_status);
}

[[nodiscard]] bool is_known_target_runtime(const std::byte value) noexcept {
    return value <= static_cast<std::byte>(protocol::TargetRuntime::unity_mono);
}

[[nodiscard]] bool is_known_time_backend(const std::byte value) noexcept {
    return value <= static_cast<std::byte>(protocol::TimeBackend::unity_mono);
}

[[nodiscard]] bool is_known_effect_state(const std::byte value) noexcept {
    return value <= static_cast<std::byte>(protocol::EffectState::failed);
}

}  // namespace

HeaderParseResult parse_header(
    const std::span<const std::byte> bytes,
    FrameHeader& output) noexcept {
    if (bytes.size() < protocol::frame_header_bytes) {
        return HeaderParseResult::truncated;
    }
    if (!std::equal(
            protocol::frame_magic.begin(),
            protocol::frame_magic.end(),
            bytes.begin())) {
        return HeaderParseResult::invalid_magic;
    }

    const auto major = read_little_endian<std::uint16_t>(bytes, 4);
    const auto minor = read_little_endian<std::uint16_t>(bytes, 6);
    if (major != protocol::protocol_major || minor != protocol::protocol_minor) {
        return HeaderParseResult::unsupported_version;
    }
    if (read_little_endian<std::uint16_t>(bytes, 8) !=
        protocol::frame_header_bytes) {
        return HeaderParseResult::invalid_header_size;
    }

    const auto raw_message_type = read_little_endian<std::uint16_t>(bytes, 10);
    if (!is_known_message_type(raw_message_type)) {
        return HeaderParseResult::unknown_message_type;
    }
    if (read_little_endian<std::uint32_t>(bytes, 12) !=
        protocol::known_frame_flags) {
        return HeaderParseResult::unknown_flags;
    }

    const auto payload_bytes = read_little_endian<std::uint32_t>(bytes, 16);
    if (payload_bytes > protocol::max_payload_bytes) {
        return HeaderParseResult::payload_too_large;
    }
    if (std::any_of(
            bytes.begin() + 52,
            bytes.begin() + protocol::frame_header_bytes,
            [](const std::byte value) { return value != std::byte{0}; })) {
        return HeaderParseResult::reserved_not_zero;
    }

    output.message_type = static_cast<protocol::MessageType>(raw_message_type);
    output.payload_bytes = payload_bytes;
    std::copy_n(bytes.begin() + 20, output.session_id.size(), output.session_id.begin());
    output.request_id = read_little_endian<std::uint64_t>(bytes, 36);
    output.command_sequence = read_little_endian<std::uint64_t>(bytes, 44);
    return HeaderParseResult::ok;
}

std::array<std::byte, protocol::frame_header_bytes> encode_header(
    const FrameHeader& header) noexcept {
    std::array<std::byte, protocol::frame_header_bytes> output{};
    std::copy(
        protocol::frame_magic.begin(),
        protocol::frame_magic.end(),
        output.begin());
    write_little_endian(protocol::protocol_major, output, 4);
    write_little_endian(protocol::protocol_minor, output, 6);
    write_little_endian(protocol::frame_header_bytes, output, 8);
    write_little_endian(
        static_cast<std::uint16_t>(header.message_type),
        output,
        10);
    write_little_endian(protocol::known_frame_flags, output, 12);
    write_little_endian(header.payload_bytes, output, 16);
    std::copy(header.session_id.begin(), header.session_id.end(), output.begin() + 20);
    write_little_endian(header.request_id, output, 36);
    write_little_endian(header.command_sequence, output, 44);
    return output;
}

bool sessions_equal(const SessionId& left, const SessionId& right) noexcept {
    std::byte difference{};
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == std::byte{0};
}

void encode_hello_payload(
    const HelloPayload& payload,
    const std::span<std::byte, hello_payload_bytes> output) noexcept {
    std::fill(output.begin(), output.end(), std::byte{0});
    std::copy(payload.token.begin(), payload.token.end(), output.begin());
    write_little_endian(payload.process_id, output, 32);
}

bool decode_hello_payload(
    const std::span<const std::byte> input,
    HelloPayload& output) noexcept {
    if (input.size() != hello_payload_bytes ||
        std::any_of(
            input.begin() + 36,
            input.end(),
            [](const std::byte value) { return value != std::byte{0}; })) {
        return false;
    }

    std::copy_n(input.begin(), output.token.size(), output.token.begin());
    output.process_id = read_little_endian<std::uint32_t>(input, 32);
    return true;
}

void encode_state_payload(
    const StatePayload& payload,
    const std::span<std::byte, state_payload_bytes> output) noexcept {
    std::fill(output.begin(), output.end(), std::byte{0});
    write_little_endian(std::bit_cast<std::uint64_t>(payload.configured_speed), output, 0);
    write_little_endian(std::bit_cast<std::uint64_t>(payload.effective_speed), output, 8);
    write_little_endian(payload.clock_revision, output, 16);
    write_little_endian(payload.last_applied_command_sequence, output, 24);
    output[32] = payload.paused ? std::byte{1} : std::byte{0};
    output[33] = payload.accepting_commands ? std::byte{1} : std::byte{0};
    output[34] =
        payload.disconnect_recovery_occurred ? std::byte{1} : std::byte{0};
    output[35] = static_cast<std::byte>(payload.target_runtime);
    output[36] = static_cast<std::byte>(payload.time_backend);
    output[37] = static_cast<std::byte>(payload.effect_state);
}

bool decode_state_payload(
    const std::span<const std::byte> input,
    StatePayload& output) noexcept {
    if (input.size() != state_payload_bytes || input[32] > std::byte{1} ||
        input[33] > std::byte{1} || input[34] > std::byte{1} ||
        !is_known_target_runtime(input[35]) ||
        !is_known_time_backend(input[36]) ||
        !is_known_effect_state(input[37]) ||
        std::any_of(
            input.begin() + 38,
            input.end(),
            [](const std::byte value) { return value != std::byte{0}; })) {
        return false;
    }

    output.configured_speed =
        std::bit_cast<double>(read_little_endian<std::uint64_t>(input, 0));
    output.effective_speed =
        std::bit_cast<double>(read_little_endian<std::uint64_t>(input, 8));
    output.clock_revision = read_little_endian<std::uint64_t>(input, 16);
    output.last_applied_command_sequence =
        read_little_endian<std::uint64_t>(input, 24);
    output.paused = input[32] == std::byte{1};
    output.accepting_commands = input[33] == std::byte{1};
    output.disconnect_recovery_occurred = input[34] == std::byte{1};
    output.target_runtime = static_cast<protocol::TargetRuntime>(input[35]);
    output.time_backend = static_cast<protocol::TimeBackend>(input[36]);
    output.effect_state = static_cast<protocol::EffectState>(input[37]);

    return std::isfinite(output.configured_speed) &&
           std::isfinite(output.effective_speed);
}

void encode_error_payload(
    const protocol::ErrorCode error,
    const std::span<std::byte, error_payload_bytes> output) noexcept {
    write_little_endian(static_cast<std::uint32_t>(error), output, 0);
}

bool decode_error_payload(
    const std::span<const std::byte> input,
    protocol::ErrorCode& output) noexcept {
    if (input.size() != error_payload_bytes) {
        return false;
    }

    output = static_cast<protocol::ErrorCode>(
        read_little_endian<std::uint32_t>(input, 0));
    return true;
}

void encode_speed_payload(
    const double speed,
    const std::span<std::byte, sizeof(double)> output) noexcept {
    write_little_endian(std::bit_cast<std::uint64_t>(speed), output, 0);
}

bool decode_speed_payload(
    const std::span<const std::byte> input,
    double& output) noexcept {
    if (input.size() != sizeof(double)) {
        return false;
    }

    output = std::bit_cast<double>(read_little_endian<std::uint64_t>(input, 0));
    return true;
}

void encode_checkpoint_payload(const CheckpointPayload& v, const std::span<std::byte> b) noexcept {
    write_little_endian<std::uint32_t>(v.available ? 1U : 0U, b, 0);
    write_little_endian<std::uint32_t>(v.has_snapshot ? 1U : 0U, b, 4);
    write_little_endian(v.operation, b, 8);
    write_little_endian(static_cast<std::uint32_t>(v.error), b, 12);
}
bool decode_checkpoint_payload(const std::span<const std::byte> b, CheckpointPayload& v) noexcept {
    if (b.size() != checkpoint_payload_bytes) return false;
    const auto available = read_little_endian<std::uint32_t>(b, 0);
    const auto saved = read_little_endian<std::uint32_t>(b, 4);
    const auto operation = read_little_endian<std::uint32_t>(b, 8);
    if (available > 1 || saved > 1 || operation > 2) return false;
    v = {available != 0, saved != 0, operation,
         static_cast<protocol::ErrorCode>(read_little_endian<std::uint32_t>(b, 12))};
    return true;
}

}  // namespace timetransle::ipc
