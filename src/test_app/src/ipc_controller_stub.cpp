#include <Windows.h>
#include <sddl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "timetransle/ipc/pipe_io.hpp"
#include "timetransle/ipc/protocol.hpp"

namespace {

using timetransle::ipc::Frame;
using timetransle::ipc::FrameHeader;
using timetransle::ipc::PipeIoResult;
using timetransle::ipc::SessionId;
using timetransle::ipc::SessionToken;
using timetransle::protocol::ErrorCode;
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

class LocalMemory final {
public:
    explicit LocalMemory(HLOCAL memory = nullptr) noexcept : memory_(memory) {}
    ~LocalMemory() {
        if (memory_ != nullptr) {
            LocalFree(memory_);
        }
    }

    LocalMemory(const LocalMemory&) = delete;
    LocalMemory& operator=(const LocalMemory&) = delete;

    [[nodiscard]] HLOCAL get() const noexcept {
        return memory_;
    }

private:
    HLOCAL memory_{};
};

[[nodiscard]] std::uint8_t hex_nibble(const wchar_t value) {
    if (value >= L'0' && value <= L'9') {
        return static_cast<std::uint8_t>(value - L'0');
    }
    if (value >= L'a' && value <= L'f') {
        return static_cast<std::uint8_t>(value - L'a' + 10);
    }
    if (value >= L'A' && value <= L'F') {
        return static_cast<std::uint8_t>(value - L'A' + 10);
    }
    throw TestFailure("invalid hexadecimal argument");
}

template <std::size_t Size>
[[nodiscard]] std::array<std::byte, Size> parse_hex(const std::wstring_view text) {
    REQUIRE(text.size() == Size * 2);
    std::array<std::byte, Size> output{};
    for (std::size_t index = 0; index < Size; ++index) {
        output[index] = static_cast<std::byte>(
            static_cast<std::uint8_t>(
                (hex_nibble(text[index * 2]) << 4U) |
                hex_nibble(text[index * 2 + 1])));
    }
    return output;
}

[[nodiscard]] std::wstring current_user_sddl() {
    HANDLE raw_token{};
    REQUIRE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) != FALSE);
    UniqueHandle token{raw_token};

    DWORD required{};
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &required);
    REQUIRE(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    std::vector<std::byte> buffer(required);
    REQUIRE(GetTokenInformation(
                token.get(),
                TokenUser,
                buffer.data(),
                required,
                &required) != FALSE);
    const auto* const token_user =
        reinterpret_cast<const TOKEN_USER*>(buffer.data());

    LPWSTR raw_sid{};
    REQUIRE(ConvertSidToStringSidW(token_user->User.Sid, &raw_sid) != FALSE);
    LocalMemory sid_memory{raw_sid};
    static_cast<void>(sid_memory);
    return L"D:P(A;;GA;;;" + std::wstring{raw_sid} + L")(A;;GA;;;SY)";
}

[[nodiscard]] UniqueHandle create_server_pipe(const std::wstring& pipe_name) {
    const auto sddl = current_user_sddl();
    PSECURITY_DESCRIPTOR raw_descriptor{};
    REQUIRE(ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl.c_str(),
                SDDL_REVISION_1,
                &raw_descriptor,
                nullptr) != FALSE);
    LocalMemory descriptor{raw_descriptor};
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = raw_descriptor,
        .bInheritHandle = FALSE,
    };

    const auto pipe = CreateNamedPipeW(
        pipe_name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,
        65'600,
        65'600,
        0,
        &security);
    REQUIRE(pipe != INVALID_HANDLE_VALUE);
    return UniqueHandle{pipe};
}

void send_frame(
    const HANDLE pipe,
    const SessionId& session_id,
    const MessageType type,
    const std::uint64_t request_id,
    const std::uint64_t sequence,
    const std::span<const std::byte> payload = {}) {
    REQUIRE(timetransle::ipc::write_frame(
                pipe,
                FrameHeader{
                    .message_type = type,
                    .payload_bytes = static_cast<std::uint32_t>(payload.size()),
                    .session_id = session_id,
                    .request_id = request_id,
                    .command_sequence = sequence,
                },
                payload) == PipeIoResult::ok);
}

[[nodiscard]] Frame receive_frame(const HANDLE pipe) {
    Frame frame;
    REQUIRE(timetransle::ipc::read_frame(pipe, frame) == PipeIoResult::ok);
    return frame;
}

[[nodiscard]] timetransle::ipc::StatePayload receive_state(
    const HANDLE pipe,
    const MessageType expected_type,
    const std::uint64_t expected_sequence) {
    const auto frame = receive_frame(pipe);
    REQUIRE(frame.header.message_type == expected_type);
    REQUIRE(frame.header.command_sequence == expected_sequence);
    timetransle::ipc::StatePayload state;
    REQUIRE(timetransle::ipc::decode_state_payload(
        std::span{frame.payload}.first(frame.header.payload_bytes),
        state));
    return state;
}

void receive_error(
    const HANDLE pipe,
    const ErrorCode expected_error,
    const std::uint64_t expected_sequence) {
    const auto frame = receive_frame(pipe);
    REQUIRE(frame.header.message_type == MessageType::error);
    REQUIRE(frame.header.command_sequence == expected_sequence);
    ErrorCode error{};
    REQUIRE(timetransle::ipc::decode_error_payload(
        std::span{frame.payload}.first(frame.header.payload_bytes),
        error));
    REQUIRE(error == expected_error);
}

void perform_handshake(
    const HANDLE pipe,
    const SessionId& session_id,
    const SessionToken& token) {
    const auto hello_frame = receive_frame(pipe);
    REQUIRE(hello_frame.header.message_type == MessageType::hello);
    REQUIRE(timetransle::ipc::sessions_equal(
        hello_frame.header.session_id,
        session_id));

    timetransle::ipc::HelloPayload hello;
    REQUIRE(timetransle::ipc::decode_hello_payload(
        std::span{hello_frame.payload}.first(hello_frame.header.payload_bytes),
        hello));
    REQUIRE(hello.token == token);

    ULONG client_process_id{};
    REQUIRE(GetNamedPipeClientProcessId(pipe, &client_process_id) != FALSE);
    REQUIRE(client_process_id == hello.process_id);

    std::array<std::byte, timetransle::ipc::hello_payload_bytes> payload{};
    timetransle::ipc::encode_hello_payload(
        timetransle::ipc::HelloPayload{
            .token = token,
            .process_id = GetCurrentProcessId(),
        },
        payload);
    send_frame(
        pipe,
        session_id,
        MessageType::hello_ack,
        hello_frame.header.request_id,
        0,
        payload);
}

void run_disconnect_script(
    const HANDLE pipe,
    const SessionId& session_id) {
    std::array<std::byte, sizeof(double)> speed_payload{};
    timetransle::ipc::encode_speed_payload(
        std::numeric_limits<double>::quiet_NaN(),
        speed_payload);
    send_frame(pipe, session_id, MessageType::set_speed, 2, 1, speed_payload);
    receive_error(pipe, ErrorCode::invalid_speed, 1);

    timetransle::ipc::encode_speed_payload(2.0, speed_payload);
    send_frame(pipe, session_id, MessageType::set_speed, 3, 1, speed_payload);
    auto state = receive_state(pipe, MessageType::set_speed_ack, 1);
    REQUIRE(state.configured_speed == 2.0);

    timetransle::ipc::encode_speed_payload(8.0, speed_payload);
    send_frame(pipe, session_id, MessageType::set_speed, 4, 1, speed_payload);
    receive_error(pipe, ErrorCode::stale_sequence, 1);

    send_frame(pipe, session_id, MessageType::pause, 5, 2);
    state = receive_state(pipe, MessageType::pause_ack, 2);
    REQUIRE(state.paused);
    send_frame(pipe, session_id, MessageType::resume, 6, 3);
    state = receive_state(pipe, MessageType::resume_ack, 3);
    REQUIRE(!state.paused);
    send_frame(pipe, session_id, MessageType::reset, 7, 4);
    state = receive_state(pipe, MessageType::reset_ack, 4);
    REQUIRE(state.configured_speed == 1.0);

    timetransle::ipc::encode_speed_payload(2.0, speed_payload);
    send_frame(pipe, session_id, MessageType::set_speed, 8, 5, speed_payload);
    state = receive_state(pipe, MessageType::set_speed_ack, 5);
    REQUIRE(state.configured_speed == 2.0);
    Sleep(500);
}

void run_old_session_script(
    const HANDLE pipe,
    SessionId session_id) {
    session_id[0] ^= std::byte{0xFF};
    send_frame(pipe, session_id, MessageType::get_status, 2, 0);
    receive_error(pipe, ErrorCode::session_mismatch, 0);
}

void run_shutdown_script(
    const HANDLE pipe,
    const SessionId& session_id) {
    std::array<std::byte, sizeof(double)> speed_payload{};
    timetransle::ipc::encode_speed_payload(2.0, speed_payload);
    send_frame(pipe, session_id, MessageType::set_speed, 2, 1, speed_payload);
    const auto running = receive_state(pipe, MessageType::set_speed_ack, 1);
    REQUIRE(running.configured_speed == 2.0);

    send_frame(pipe, session_id, MessageType::shutdown, 3, 2);
    const auto shutdown = receive_state(pipe, MessageType::shutdown_ack, 2);
    REQUIRE(shutdown.configured_speed == 1.0);
    REQUIRE(!shutdown.paused);
    REQUIRE(!shutdown.accepting_commands);
}

void run_server(
    const std::wstring& mode,
    const std::wstring& pipe_name,
    const SessionId& session_id,
    const SessionToken& token) {
    auto pipe = create_server_pipe(pipe_name);
    const auto connected = ConnectNamedPipe(pipe.get(), nullptr);
    REQUIRE(connected != FALSE || GetLastError() == ERROR_PIPE_CONNECTED);
    perform_handshake(pipe.get(), session_id, token);

    if (mode == L"disconnect") {
        run_disconnect_script(pipe.get(), session_id);
    } else if (mode == L"old-session") {
        run_old_session_script(pipe.get(), session_id);
    } else if (mode == L"shutdown") {
        run_shutdown_script(pipe.get(), session_id);
    } else {
        throw TestFailure("unknown controller stub mode");
    }
}

}  // namespace

int wmain(const int argument_count, wchar_t** const arguments) {
    if (argument_count != 5) {
        std::cerr
            << "usage: ControllerStub <mode> <pipe> <session-hex> <token-hex>\n";
        return 2;
    }

    try {
        run_server(
            arguments[1],
            arguments[2],
            parse_hex<16>(arguments[3]),
            parse_hex<32>(arguments[4]));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Controller stub failed: " << error.what() << '\n';
        return 1;
    }
}
