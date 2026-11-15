#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "timetransle/ipc/pipe_io.hpp"
#include "timetransle/ipc/command_queue.hpp"
#include "timetransle/ipc/protocol.hpp"

namespace {

using timetransle::ipc::Frame;
using timetransle::ipc::FrameHeader;
using timetransle::ipc::HeaderParseResult;
using timetransle::ipc::PipeIoResult;
using timetransle::protocol::MessageType;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(
    const bool condition,
    const std::string_view expression,
    const std::source_location location = std::source_location::current()) {
    if (!condition) {
        throw TestFailure(
            std::string{location.file_name()} + ':' + std::to_string(location.line()) +
            " requirement failed: " + std::string{expression});
    }
}

#define REQUIRE(expression) require((expression), #expression)

class UniqueHandle final {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(const HANDLE handle) noexcept : handle_(handle) {}
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

[[nodiscard]] std::vector<std::byte> from_hex(const std::string_view hex) {
    REQUIRE(hex.size() % 2 == 0);
    std::vector<std::byte> output;
    output.reserve(hex.size() / 2);

    const auto nibble = [](const char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(value - 'a' + 10);
        }
        throw TestFailure("invalid hex");
    };

    for (std::size_t index = 0; index < hex.size(); index += 2) {
        output.push_back(static_cast<std::byte>(
            static_cast<std::uint8_t>((nibble(hex[index]) << 4U) |
                                      nibble(hex[index + 1]))));
    }
    return output;
}

[[nodiscard]] FrameHeader vector_header() {
    FrameHeader header{
        .message_type = MessageType::set_speed,
        .payload_bytes = 8,
        .request_id = 0x0102030405060708ULL,
        .command_sequence = 42,
    };
    for (std::size_t index = 0; index < header.session_id.size(); ++index) {
        header.session_id[index] = static_cast<std::byte>(index);
    }
    return header;
}

void contract_vector_matches_exact_wire_bytes() {
    constexpr std::string_view frame_hex =
        "54544c4501000200400005000000000008000000"
        "000102030405060708090a0b0c0d0e0f"
        "08070605040302012a00000000000000"
        "000000000000000000000000"
        "000000000000e03f";
    const auto expected = from_hex(frame_hex);
    REQUIRE(expected.size() == 72);

    const auto header = vector_header();
    const auto encoded_header = timetransle::ipc::encode_header(header);
    std::array<std::byte, sizeof(double)> speed_payload{};
    timetransle::ipc::encode_speed_payload(0.5, speed_payload);

    REQUIRE(std::equal(
        encoded_header.begin(),
        encoded_header.end(),
        expected.begin()));
    REQUIRE(std::equal(
        speed_payload.begin(),
        speed_payload.end(),
        expected.begin() + encoded_header.size()));

    FrameHeader parsed;
    REQUIRE(timetransle::ipc::parse_header(encoded_header, parsed) ==
            HeaderParseResult::ok);
    REQUIRE(parsed.message_type == MessageType::set_speed);
    REQUIRE(parsed.payload_bytes == 8);
    REQUIRE(parsed.request_id == header.request_id);
    REQUIRE(parsed.command_sequence == 42);
    REQUIRE(timetransle::ipc::sessions_equal(parsed.session_id, header.session_id));
}

void malformed_headers_are_rejected_before_payload_use() {
    const auto valid = timetransle::ipc::encode_header(vector_header());
    FrameHeader parsed;

    for (std::size_t size = 0; size < valid.size(); ++size) {
        REQUIRE(timetransle::ipc::parse_header(
                    std::span{valid}.first(size),
                    parsed) == HeaderParseResult::truncated);
    }

    auto malformed = valid;
    malformed[0] = std::byte{'X'};
    REQUIRE(timetransle::ipc::parse_header(malformed, parsed) ==
            HeaderParseResult::invalid_magic);

    malformed = valid;
    malformed[4] = std::byte{2};
    REQUIRE(timetransle::ipc::parse_header(malformed, parsed) ==
            HeaderParseResult::unsupported_version);

    malformed = valid;
    malformed[8] = std::byte{63};
    REQUIRE(timetransle::ipc::parse_header(malformed, parsed) ==
            HeaderParseResult::invalid_header_size);

    malformed = valid;
    malformed[10] = std::byte{99};
    REQUIRE(timetransle::ipc::parse_header(malformed, parsed) ==
            HeaderParseResult::unknown_message_type);

    malformed = valid;
    malformed[12] = std::byte{1};
    REQUIRE(timetransle::ipc::parse_header(malformed, parsed) ==
            HeaderParseResult::unknown_flags);

    malformed = valid;
    malformed[16] = std::byte{1};
    malformed[18] = std::byte{1};
    REQUIRE(timetransle::ipc::parse_header(malformed, parsed) ==
            HeaderParseResult::payload_too_large);

    malformed = valid;
    malformed[63] = std::byte{1};
    REQUIRE(timetransle::ipc::parse_header(malformed, parsed) ==
            HeaderParseResult::reserved_not_zero);
}

void payload_codecs_are_strict_and_round_trip() {
    timetransle::ipc::HelloPayload hello;
    for (std::size_t index = 0; index < hello.token.size(); ++index) {
        hello.token[index] = static_cast<std::byte>(index * 3U);
    }
    hello.process_id = 1234;

    std::array<std::byte, timetransle::ipc::hello_payload_bytes> hello_bytes{};
    timetransle::ipc::encode_hello_payload(hello, hello_bytes);
    timetransle::ipc::HelloPayload decoded_hello;
    REQUIRE(timetransle::ipc::decode_hello_payload(hello_bytes, decoded_hello));
    REQUIRE(decoded_hello.token == hello.token);
    REQUIRE(decoded_hello.process_id == hello.process_id);
    hello_bytes.back() = std::byte{1};
    REQUIRE(!timetransle::ipc::decode_hello_payload(hello_bytes, decoded_hello));

    const timetransle::ipc::StatePayload state{
        .configured_speed = 2.0,
        .effective_speed = 0.0,
        .clock_revision = 9,
        .last_applied_command_sequence = 42,
        .paused = true,
        .accepting_commands = true,
        .disconnect_recovery_occurred = false,
        .target_runtime = timetransle::protocol::TargetRuntime::unity_mono,
        .time_backend = timetransle::protocol::TimeBackend::unity_mono,
        .effect_state = timetransle::protocol::EffectState::pending,
    };
    std::array<std::byte, timetransle::ipc::state_payload_bytes> state_bytes{};
    timetransle::ipc::encode_state_payload(state, state_bytes);
    timetransle::ipc::StatePayload decoded_state;
    REQUIRE(timetransle::ipc::decode_state_payload(state_bytes, decoded_state));
    REQUIRE(decoded_state.configured_speed == 2.0);
    REQUIRE(decoded_state.effective_speed == 0.0);
    REQUIRE(decoded_state.clock_revision == 9);
    REQUIRE(decoded_state.last_applied_command_sequence == 42);
    REQUIRE(decoded_state.paused);
    REQUIRE(decoded_state.target_runtime ==
            timetransle::protocol::TargetRuntime::unity_mono);
    REQUIRE(decoded_state.time_backend ==
            timetransle::protocol::TimeBackend::unity_mono);
    REQUIRE(decoded_state.effect_state ==
            timetransle::protocol::EffectState::pending);

    state_bytes[37] = std::byte{0xFF};
    REQUIRE(!timetransle::ipc::decode_state_payload(state_bytes, decoded_state));
    timetransle::ipc::encode_state_payload(state, state_bytes);
    state_bytes[38] = std::byte{1};
    REQUIRE(!timetransle::ipc::decode_state_payload(state_bytes, decoded_state));

    std::array<std::byte, sizeof(double)> speed_bytes{};
    timetransle::ipc::encode_speed_payload(
        std::numeric_limits<double>::quiet_NaN(),
        speed_bytes);
    double speed{};
    REQUIRE(timetransle::ipc::decode_speed_payload(speed_bytes, speed));
    REQUIRE(std::isnan(speed));
    REQUIRE(!timetransle::ipc::decode_speed_payload(
        std::span<const std::byte>{speed_bytes}.first(7),
        speed));
}

void pipe_io_handles_fragmentation_sticky_frames_and_truncation() {
    HANDLE read_handle_raw{};
    HANDLE write_handle_raw{};
    REQUIRE(CreatePipe(&read_handle_raw, &write_handle_raw, nullptr, 0) != FALSE);
    UniqueHandle read_handle{read_handle_raw};
    UniqueHandle write_handle{write_handle_raw};

    const auto header = vector_header();
    const auto encoded_header = timetransle::ipc::encode_header(header);
    std::array<std::byte, sizeof(double)> payload{};
    timetransle::ipc::encode_speed_payload(0.5, payload);

    std::thread writer{[&] {
        DWORD written{};
        REQUIRE(WriteFile(
                    write_handle.get(),
                    encoded_header.data(),
                    7,
                    &written,
                    nullptr) != FALSE);
        REQUIRE(WriteFile(
                    write_handle.get(),
                    encoded_header.data() + 7,
                    static_cast<DWORD>(encoded_header.size() - 7),
                    &written,
                    nullptr) != FALSE);
        REQUIRE(WriteFile(
                    write_handle.get(),
                    payload.data(),
                    static_cast<DWORD>(payload.size()),
                    &written,
                    nullptr) != FALSE);

        const auto second_header = timetransle::ipc::encode_header(header);
        REQUIRE(WriteFile(
                    write_handle.get(),
                    second_header.data(),
                    static_cast<DWORD>(second_header.size()),
                    &written,
                    nullptr) != FALSE);
        REQUIRE(WriteFile(
                    write_handle.get(),
                    payload.data(),
                    static_cast<DWORD>(payload.size()),
                    &written,
                    nullptr) != FALSE);
    }};

    Frame first;
    Frame second;
    REQUIRE(timetransle::ipc::read_frame(read_handle.get(), first) ==
            PipeIoResult::ok);
    REQUIRE(timetransle::ipc::read_frame(read_handle.get(), second) ==
            PipeIoResult::ok);
    writer.join();

    double decoded_speed{};
    REQUIRE(timetransle::ipc::decode_speed_payload(
        std::span{first.payload}.first(first.header.payload_bytes),
        decoded_speed));
    REQUIRE(decoded_speed == 0.5);
    REQUIRE(second.header.command_sequence == 42);

    HANDLE truncated_read_raw{};
    HANDLE truncated_write_raw{};
    REQUIRE(CreatePipe(
                &truncated_read_raw,
                &truncated_write_raw,
                nullptr,
                0) != FALSE);
    UniqueHandle truncated_read{truncated_read_raw};
    {
        UniqueHandle truncated_write{truncated_write_raw};
        DWORD written{};
        REQUIRE(WriteFile(
                    truncated_write.get(),
                    encoded_header.data(),
                    10,
                    &written,
                    nullptr) != FALSE);
    }

    Frame truncated_frame;
    REQUIRE(timetransle::ipc::read_frame(truncated_read.get(), truncated_frame) ==
            PipeIoResult::disconnected);
}

void speed_command_queue_is_strictly_bounded_and_keeps_latest_pending() {
    timetransle::ipc::BoundedSpeedCommandQueue queue;
    REQUIRE(queue.capacity() == 2);
    REQUIRE(queue.submit({.speed = 0.5, .ui_sequence = 1}));
    REQUIRE(queue.in_flight()->ui_sequence == 1);

    for (std::uint64_t sequence = 2; sequence <= 10'000; ++sequence) {
        REQUIRE(!queue.submit({
            .speed = static_cast<double>(sequence) / 10'000.0,
            .ui_sequence = sequence,
        }));
    }

    REQUIRE(queue.in_flight()->ui_sequence == 1);
    REQUIRE(queue.pending()->ui_sequence == 10'000);
    REQUIRE(!queue.acknowledge(999).has_value());
    REQUIRE(queue.in_flight()->ui_sequence == 1);

    const auto promoted = queue.acknowledge(1);
    REQUIRE(promoted.has_value());
    REQUIRE(promoted->ui_sequence == 10'000);
    REQUIRE(queue.in_flight()->ui_sequence == 10'000);
    REQUIRE(!queue.pending().has_value());

    REQUIRE(!queue.acknowledge(10'000).has_value());
    REQUIRE(!queue.in_flight().has_value());
}

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

}  // namespace

int main() {
    constexpr std::array tests{
        TestCase{"exact IPC contract vector", &contract_vector_matches_exact_wire_bytes},
        TestCase{"malformed header rejection",
                 &malformed_headers_are_rejected_before_payload_use},
        TestCase{"strict payload codecs", &payload_codecs_are_strict_and_round_trip},
        TestCase{"fragmented and sticky pipe IO",
                 &pipe_io_handles_fragmentation_sticky_frames_and_truncation},
        TestCase{"bounded latest-value command queue",
                 &speed_command_queue_is_strictly_bounded_and_keeps_latest_pending},
    };

    std::size_t failures = 0;
    for (const auto& test : tests) {
        try {
            test.function();
            std::cout << "[pass] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[fail] " << test.name << ": " << error.what() << '\n';
        }
    }

    if (failures != 0) {
        std::cerr << failures << " IPC test(s) failed\n";
        return 1;
    }

    std::cout << tests.size() << " IPC tests passed\n";
    return 0;
}
