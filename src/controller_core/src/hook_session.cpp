#include "timetransle/controller/hook_session.hpp"

#include <Windows.h>
#include <bcrypt.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "timetransle/ipc/bootstrap.hpp"
#include "timetransle/ipc/protocol.hpp"
#include "timetransle/protocol/constants.hpp"

namespace timetransle::controller {
namespace {

constexpr DWORD operation_timeout_ms = 5'000;
constexpr DWORD injector_timeout_ms = 15'000;
constexpr std::size_t maximum_pending_commands = 32;
constexpr auto unity_pending_poll_interval = std::chrono::milliseconds{16};
constexpr auto unity_stalled_poll_interval = std::chrono::milliseconds{250};
constexpr auto unity_stalled_after = std::chrono::seconds{2};

class UniqueHandle final {
public:
    explicit UniqueHandle(const HANDLE handle = nullptr) noexcept
        : handle_{handle} {}

    ~UniqueHandle() {
        reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_{other.release()} {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    [[nodiscard]] HANDLE release() noexcept {
        const auto value = handle_;
        handle_ = nullptr;
        return value;
    }

    void reset(const HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_{};
};

class LocalMemory final {
public:
    explicit LocalMemory(HLOCAL memory = nullptr) noexcept
        : memory_{memory} {}

    ~LocalMemory() {
        if (memory_ != nullptr) {
            LocalFree(memory_);
        }
    }

    LocalMemory(const LocalMemory&) = delete;
    LocalMemory& operator=(const LocalMemory&) = delete;

private:
    HLOCAL memory_{};
};

class AttributeList final {
public:
    AttributeList() {
        SIZE_T bytes{};
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            bytes == 0) {
            return;
        }
        storage_.resize(bytes);
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            storage_.data());
        if (InitializeProcThreadAttributeList(
                list_,
                1,
                0,
                &bytes) == FALSE) {
            list_ = nullptr;
            storage_.clear();
        }
    }

    ~AttributeList() {
        if (list_ != nullptr) {
            DeleteProcThreadAttributeList(list_);
        }
    }

    AttributeList(const AttributeList&) = delete;
    AttributeList& operator=(const AttributeList&) = delete;

    [[nodiscard]] bool set_handle(const HANDLE handle) noexcept {
        inherited_handle_ = handle;
        return list_ != nullptr &&
               UpdateProcThreadAttribute(
                   list_,
                   0,
                   PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                   &inherited_handle_,
                   sizeof(inherited_handle_),
                   nullptr,
                   nullptr) != FALSE;
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept {
        return list_;
    }

private:
    std::vector<std::byte> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
    HANDLE inherited_handle_{};
};

[[nodiscard]] constexpr std::uint64_t file_time_to_uint64(
    const FILETIME value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           value.dwLowDateTime;
}

[[nodiscard]] std::optional<std::uint64_t>
current_process_creation_time() noexcept {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(
            GetCurrentProcess(),
            &creation,
            &exit,
            &kernel,
            &user) == FALSE) {
        return std::nullopt;
    }
    return file_time_to_uint64(creation);
}

[[nodiscard]] bool fill_random(
    const std::span<std::byte> output) noexcept {
    if (output.size() > std::numeric_limits<ULONG>::max()) {
        return false;
    }
    return BCryptGenRandom(
               nullptr,
               reinterpret_cast<PUCHAR>(output.data()),
               static_cast<ULONG>(output.size()),
               BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

[[nodiscard]] std::wstring hex_text(
    const std::span<const std::byte> bytes) {
    constexpr wchar_t digits[]{L"0123456789abcdef"};
    std::wstring output;
    output.reserve(bytes.size() * 2);
    for (const auto value : bytes) {
        const auto byte = std::to_integer<std::uint8_t>(value);
        output.push_back(digits[byte >> 4U]);
        output.push_back(digits[byte & 0x0FU]);
    }
    return output;
}

[[nodiscard]] std::optional<std::wstring> current_user_sddl() {
    HANDLE raw_token{};
    if (OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &raw_token) == FALSE) {
        return std::nullopt;
    }
    UniqueHandle token{raw_token};

    DWORD required{};
    GetTokenInformation(
        token.get(),
        TokenUser,
        nullptr,
        0,
        &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        required == 0) {
        return std::nullopt;
    }
    std::vector<std::byte> buffer(required);
    if (GetTokenInformation(
            token.get(),
            TokenUser,
            buffer.data(),
            required,
            &required) == FALSE) {
        return std::nullopt;
    }
    const auto* const user =
        reinterpret_cast<const TOKEN_USER*>(buffer.data());

    LPWSTR raw_sid{};
    if (ConvertSidToStringSidW(user->User.Sid, &raw_sid) == FALSE) {
        return std::nullopt;
    }
    LocalMemory sid{raw_sid};
    static_cast<void>(sid);
    return L"D:P(A;;GA;;;" + std::wstring{raw_sid} +
           L")(A;;GA;;;SY)";
}

[[nodiscard]] UniqueHandle create_server_pipe(
    const std::wstring& pipe_name) {
    const auto sddl = current_user_sddl();
    if (!sddl.has_value()) {
        return UniqueHandle{INVALID_HANDLE_VALUE};
    }

    PSECURITY_DESCRIPTOR raw_descriptor{};
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl->c_str(),
            SDDL_REVISION_1,
            &raw_descriptor,
            nullptr) == FALSE) {
        return UniqueHandle{INVALID_HANDLE_VALUE};
    }
    LocalMemory descriptor{raw_descriptor};
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = raw_descriptor,
        .bInheritHandle = FALSE,
    };
    return UniqueHandle{CreateNamedPipeW(
        pipe_name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE |
            FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1,
        65'600,
        65'600,
        0,
        &security)};
}

[[nodiscard]] std::wstring quote_argument(
    const std::wstring_view argument) {
    std::wstring output{L'"'};
    for (const auto character : argument) {
        if (character == L'"') {
            output.push_back(L'\\');
        }
        output.push_back(character);
    }
    output.push_back(L'"');
    return output;
}

[[nodiscard]] bool write_bootstrap(
    const HANDLE pipe,
    const ipc::InjectorBootstrapV1& bootstrap) noexcept {
    const auto bytes = std::as_bytes(std::span{&bootstrap, 1});
    std::size_t completed = 0;
    while (completed < bytes.size()) {
        DWORD written{};
        const auto remaining = bytes.size() - completed;
        if (remaining > MAXDWORD ||
            WriteFile(
                pipe,
                bytes.data() + completed,
                static_cast<DWORD>(remaining),
                &written,
                nullptr) == FALSE ||
            written == 0) {
            return false;
        }
        completed += written;
    }
    return true;
}

[[nodiscard]] bool constant_time_equal(
    const ipc::SessionToken& left,
    const ipc::SessionToken& right) noexcept {
    std::byte difference{};
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == std::byte{0};
}

enum class IoResult {
    ok,
    stopped,
    timeout,
    disconnected,
    failed,
};

[[nodiscard]] IoResult classify_pipe_error(
    const DWORD error) noexcept {
    if (error == ERROR_BROKEN_PIPE ||
        error == ERROR_PIPE_NOT_CONNECTED ||
        error == ERROR_NO_DATA ||
        error == ERROR_HANDLE_EOF) {
        return IoResult::disconnected;
    }
    if (error == ERROR_OPERATION_ABORTED) {
        return IoResult::stopped;
    }
    return IoResult::failed;
}

[[nodiscard]] protocol::ErrorCode io_error_code(
    const IoResult result) noexcept {
    switch (result) {
        case IoResult::timeout:
            return protocol::ErrorCode::timeout;
        case IoResult::disconnected:
        case IoResult::stopped:
            return protocol::ErrorCode::pipe_disconnected;
        case IoResult::ok:
            return protocol::ErrorCode::ok;
        case IoResult::failed:
            return protocol::ErrorCode::internal_error;
    }
    return protocol::ErrorCode::internal_error;
}

}  // namespace

class HookSession::Impl final {
public:
    Impl()
        : stop_event_{CreateEventW(nullptr, TRUE, FALSE, nullptr)} {}

    ~Impl() {
        stop(false);
    }

    [[nodiscard]] bool start(
        const safety::ProcessIdentity& target,
        std::filesystem::path product_directory,
        HookRequestIdentity attach_request,
        HookEventCallback callback, const bool checkpoint_mode) {
        {
            std::lock_guard lock{mutex_};
            if (running_) {
                return false;
            }
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        std::lock_guard lock{mutex_};
        if (running_ || stop_event_.get() == nullptr ||
            (target.machine != safety::MachineType::x86 &&
             target.machine != safety::MachineType::x64) ||
            target.process_id == 0 ||
            target.creation_time == 0 ||
            !callback) {
            return false;
        }

        target_ = target;
        product_directory_ = std::move(product_directory);
        attach_request_ = std::move(attach_request);
        callback_ = std::move(callback);
        checkpoint_mode_ = checkpoint_mode;
        commands_.clear();
        stop_requested_ = false;
        graceful_shutdown_ = false;
        request_id_ = 1;
        command_sequence_ = 0;
        connected_.store(false, std::memory_order_release);
        ResetEvent(stop_event_.get());
        running_ = true;
        worker_ = std::thread([this] { worker_main(); });
        return true;
    }

    [[nodiscard]] bool submit(HookCommand command) {
        std::lock_guard lock{mutex_};
        if (!running_ ||
            !connected_.load(std::memory_order_acquire) ||
            stop_requested_ ||
            graceful_shutdown_) {
            return false;
        }

        if (command.type == HookCommandType::set_speed) {
            if (!commands_.empty() &&
                commands_.back().type ==
                    HookCommandType::set_speed) {
                commands_.back() = std::move(command);
                condition_.notify_one();
                return true;
            }
        }
        if (commands_.size() >= maximum_pending_commands) {
            return false;
        }
        commands_.push_back(std::move(command));
        condition_.notify_one();
        return true;
    }

    void stop(const bool graceful) noexcept {
        {
            std::lock_guard lock{mutex_};
            if (!running_) {
                if (worker_.joinable()) {
                    worker_.join();
                }
                return;
            }
            commands_.clear();
            if (graceful &&
                connected_.load(std::memory_order_acquire)) {
                graceful_shutdown_ = true;
                commands_.push_back(HookCommand{
                    .type = HookCommandType::shutdown,
                    .request = attach_request_,
                    .notify = false,
                });
            } else {
                stop_requested_ = true;
                SetEvent(stop_event_.get());
                const auto pipe =
                    active_pipe_.load(std::memory_order_acquire);
                if (pipe != nullptr && pipe != INVALID_HANDLE_VALUE) {
                    CancelIoEx(pipe, nullptr);
                }
            }
        }
        condition_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    [[nodiscard]] bool connected() const noexcept {
        return connected_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] bool should_stop() const noexcept {
        std::lock_guard lock{mutex_};
        return stop_requested_;
    }

    void emit(HookEvent event) noexcept {
        try {
            callback_(std::move(event));
        } catch (...) {
        }
    }

    [[nodiscard]] IoResult transfer(
        const HANDLE pipe,
        const bool write,
        const std::span<std::byte> bytes,
        const DWORD timeout_ms) const noexcept {
        std::size_t completed = 0;
        while (completed < bytes.size()) {
            UniqueHandle event{
                CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            if (event.get() == nullptr) {
                return IoResult::failed;
            }
            OVERLAPPED overlapped{.hEvent = event.get()};
            const auto remaining = bytes.size() - completed;
            if (remaining > MAXDWORD) {
                return IoResult::failed;
            }
            const auto buffer = bytes.data() + completed;
            const auto requested = static_cast<DWORD>(remaining);
            const auto issued = write
                                    ? WriteFile(
                                          pipe,
                                          buffer,
                                          requested,
                                          nullptr,
                                          &overlapped)
                                    : ReadFile(
                                          pipe,
                                          buffer,
                                          requested,
                                          nullptr,
                                          &overlapped);
            if (issued != FALSE) {
                DWORD transferred{};
                if (GetOverlappedResult(
                        pipe,
                        &overlapped,
                        &transferred,
                        FALSE) == FALSE) {
                    return classify_pipe_error(GetLastError());
                }
                if (transferred == 0) {
                    return IoResult::disconnected;
                }
                completed += transferred;
                continue;
            }
            const auto error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                return classify_pipe_error(error);
            }

            const HANDLE waits[]{event.get(), stop_event_.get()};
            const auto wait =
                WaitForMultipleObjects(2, waits, FALSE, timeout_ms);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &overlapped);
                WaitForSingleObject(event.get(), INFINITE);
                return wait == WAIT_OBJECT_0 + 1
                           ? IoResult::stopped
                           : IoResult::timeout;
            }
            DWORD transferred{};
            if (GetOverlappedResult(
                    pipe,
                    &overlapped,
                    &transferred,
                    FALSE) == FALSE) {
                return classify_pipe_error(GetLastError());
            }
            if (transferred == 0) {
                return IoResult::disconnected;
            }
            completed += transferred;
        }
        return IoResult::ok;
    }

    [[nodiscard]] IoResult write_frame(
        const HANDLE pipe,
        const ipc::FrameHeader& header,
        const std::span<const std::byte> payload) const noexcept {
        if (payload.size() != header.payload_bytes ||
            payload.size() > protocol::max_payload_bytes) {
            return IoResult::failed;
        }
        auto encoded = ipc::encode_header(header);
        auto result = transfer(
            pipe,
            true,
            encoded,
            operation_timeout_ms);
        if (result != IoResult::ok || payload.empty()) {
            return result;
        }
        return transfer(
            pipe,
            true,
            {const_cast<std::byte*>(payload.data()), payload.size()},
            operation_timeout_ms);
    }

    [[nodiscard]] IoResult read_frame(
        const HANDLE pipe,
        ipc::Frame& frame) const noexcept {
        std::array<std::byte, protocol::frame_header_bytes> encoded{};
        auto result = transfer(
            pipe,
            false,
            encoded,
            operation_timeout_ms);
        if (result != IoResult::ok ||
            ipc::parse_header(encoded, frame.header) !=
                ipc::HeaderParseResult::ok) {
            return result == IoResult::ok
                       ? IoResult::failed
                       : result;
        }
        if (frame.header.payload_bytes == 0) {
            return IoResult::ok;
        }
        return transfer(
            pipe,
            false,
            std::span{frame.payload}.first(
                frame.header.payload_bytes),
            operation_timeout_ms);
    }

    [[nodiscard]] IoResult connect_pipe(
        const HANDLE pipe) const noexcept {
        UniqueHandle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (event.get() == nullptr) {
            return IoResult::failed;
        }
        OVERLAPPED overlapped{.hEvent = event.get()};
        if (ConnectNamedPipe(pipe, &overlapped) != FALSE) {
            return IoResult::ok;
        }
        const auto error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) {
            return IoResult::ok;
        }
        if (error != ERROR_IO_PENDING) {
            return classify_pipe_error(error);
        }

        const HANDLE waits[]{event.get(), stop_event_.get()};
        const auto wait = WaitForMultipleObjects(
            2,
            waits,
            FALSE,
            operation_timeout_ms);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &overlapped);
            WaitForSingleObject(event.get(), INFINITE);
            return wait == WAIT_OBJECT_0 + 1
                       ? IoResult::stopped
                       : IoResult::timeout;
        }
        DWORD transferred{};
        return GetOverlappedResult(
                   pipe,
                   &overlapped,
                   &transferred,
                   FALSE) != FALSE
                   ? IoResult::ok
                   : classify_pipe_error(GetLastError());
    }

    [[nodiscard]] protocol::ErrorCode launch_injector(
        const ipc::InjectorBootstrapV1& bootstrap) const {
        SECURITY_ATTRIBUTES security{
            .nLength = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = nullptr,
            .bInheritHandle = TRUE,
        };
        HANDLE raw_read{};
        HANDLE raw_write{};
        if (CreatePipe(
                &raw_read,
                &raw_write,
                &security,
                sizeof(bootstrap)) == FALSE) {
            return protocol::ErrorCode::internal_error;
        }
        UniqueHandle read_end{raw_read};
        UniqueHandle write_end{raw_write};
        if (SetHandleInformation(
                write_end.get(),
                HANDLE_FLAG_INHERIT,
                0) == FALSE) {
            return protocol::ErrorCode::internal_error;
        }

        const auto injector_name =
            target_.machine == safety::MachineType::x64
                ? L"TimeTransleInjector64.exe"
                : L"TimeTransleInjector32.exe";
        const auto injector_path =
            product_directory_ / injector_name;
        std::error_code file_error;
        if (!std::filesystem::is_regular_file(
                injector_path,
                file_error) ||
            file_error) {
            return protocol::ErrorCode::architecture_unsupported;
        }

        wchar_t creation_text[32]{};
        wchar_t handle_text[32]{};
        if (swprintf_s(
                creation_text,
                L"%llx",
                static_cast<unsigned long long>(
                    target_.creation_time)) < 1 ||
            swprintf_s(
                handle_text,
                L"%llx",
                static_cast<unsigned long long>(
                    reinterpret_cast<std::uintptr_t>(
                        read_end.get()))) < 1) {
            return protocol::ErrorCode::internal_error;
        }

        auto command_line =
            quote_argument(injector_path.wstring()) + L" " +
            std::to_wstring(target_.process_id) + L" " +
            creation_text +
            L" --controller-bootstrap " + handle_text;
        command_line.push_back(L'\0');

        AttributeList attributes;
        if (!attributes.set_handle(read_end.get())) {
            return protocol::ErrorCode::internal_error;
        }
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = attributes.get();
        PROCESS_INFORMATION process{};
        if (CreateProcessW(
                injector_path.c_str(),
                command_line.data(),
                nullptr,
                nullptr,
                TRUE,
                EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                nullptr,
                product_directory_.c_str(),
                &startup.StartupInfo,
                &process) == FALSE) {
            return protocol::ErrorCode::access_denied;
        }
        UniqueHandle process_handle{process.hProcess};
        UniqueHandle thread_handle{process.hThread};
        static_cast<void>(thread_handle);
        read_end.reset();

        if (!write_bootstrap(write_end.get(), bootstrap)) {
            return protocol::ErrorCode::authentication_failed;
        }
        write_end.reset();

        const HANDLE waits[]{
            process_handle.get(),
            stop_event_.get(),
        };
        const auto wait = WaitForMultipleObjects(
            2,
            waits,
            FALSE,
            injector_timeout_ms);
        if (wait == WAIT_OBJECT_0 + 1) {
            return protocol::ErrorCode::pipe_disconnected;
        }
        if (wait != WAIT_OBJECT_0) {
            return protocol::ErrorCode::timeout;
        }
        DWORD exit_code{};
        if (GetExitCodeProcess(
                process_handle.get(),
                &exit_code) == FALSE) {
            return protocol::ErrorCode::internal_error;
        }
        return static_cast<protocol::ErrorCode>(exit_code);
    }

    [[nodiscard]] protocol::ErrorCode perform_handshake(
        const HANDLE pipe,
        const ipc::InjectorBootstrapV1& bootstrap) const noexcept {
        ipc::Frame hello;
        const auto read_result = read_frame(pipe, hello);
        if (read_result != IoResult::ok) {
            return io_error_code(read_result);
        }
        ipc::HelloPayload payload;
        if (hello.header.message_type !=
                protocol::MessageType::hello ||
            hello.header.command_sequence != 0 ||
            !ipc::decode_hello_payload(
                std::span{hello.payload}.first(
                    hello.header.payload_bytes),
                payload)) {
            return protocol::ErrorCode::invalid_message;
        }
        if (!ipc::sessions_equal(
                hello.header.session_id,
                bootstrap.session_id)) {
            return protocol::ErrorCode::session_mismatch;
        }
        if (!constant_time_equal(
                payload.token,
                bootstrap.session_token)) {
            return protocol::ErrorCode::authentication_failed;
        }
        if (payload.process_id != target_.process_id) {
            return protocol::ErrorCode::target_identity_changed;
        }
        ULONG client_process_id{};
        if (GetNamedPipeClientProcessId(
                pipe,
                &client_process_id) == FALSE ||
            client_process_id != target_.process_id) {
            return protocol::ErrorCode::target_identity_changed;
        }

        std::array<std::byte, ipc::hello_payload_bytes>
            acknowledgement{};
        ipc::encode_hello_payload(
            ipc::HelloPayload{
                .token = bootstrap.session_token,
                .process_id = GetCurrentProcessId(),
            },
            acknowledgement);
        const auto result = write_frame(
            pipe,
            ipc::FrameHeader{
                .message_type =
                    protocol::MessageType::hello_ack,
                .payload_bytes = static_cast<std::uint32_t>(
                    acknowledgement.size()),
                .session_id = bootstrap.session_id,
                .request_id = hello.header.request_id,
                .command_sequence = 0,
            },
            acknowledgement);
        return io_error_code(result);
    }

    [[nodiscard]] protocol::ErrorCode transact(
        const HANDLE pipe,
        const ipc::SessionId& session_id,
        const HookCommand& command,
        HookState& output) {
        protocol::MessageType request_type{};
        protocol::MessageType response_type{};
        std::array<std::byte, sizeof(double)> speed_payload{};
        std::span<const std::byte> payload;
        auto command_sequence = command_sequence_;

        switch (command.type) {
            case HookCommandType::save_checkpoint:
                request_type=protocol::MessageType::save_checkpoint; response_type=protocol::MessageType::save_checkpoint_ack;
                command_sequence=++command_sequence_; break;
            case HookCommandType::load_checkpoint:
                request_type=protocol::MessageType::load_checkpoint; response_type=protocol::MessageType::load_checkpoint_ack;
                command_sequence=++command_sequence_; break;
            case HookCommandType::get_checkpoint_status:
                request_type=protocol::MessageType::get_checkpoint_status; response_type=protocol::MessageType::checkpoint_status;
                command_sequence=0; break;
            case HookCommandType::get_status:
                request_type = protocol::MessageType::get_status;
                response_type = protocol::MessageType::status;
                command_sequence = 0;
                break;
            case HookCommandType::set_speed:
                request_type = protocol::MessageType::set_speed;
                response_type =
                    protocol::MessageType::set_speed_ack;
                ipc::encode_speed_payload(
                    command.speed,
                    speed_payload);
                payload = speed_payload;
                command_sequence = ++command_sequence_;
                break;
            case HookCommandType::pause:
                request_type = protocol::MessageType::pause;
                response_type = protocol::MessageType::pause_ack;
                command_sequence = ++command_sequence_;
                break;
            case HookCommandType::resume:
                request_type = protocol::MessageType::resume;
                response_type = protocol::MessageType::resume_ack;
                command_sequence = ++command_sequence_;
                break;
            case HookCommandType::reset:
                request_type = protocol::MessageType::reset;
                response_type = protocol::MessageType::reset_ack;
                command_sequence = ++command_sequence_;
                break;
            case HookCommandType::shutdown:
                request_type = protocol::MessageType::shutdown;
                response_type = protocol::MessageType::shutdown_ack;
                command_sequence = ++command_sequence_;
                break;
        }

        const auto request_id = ++request_id_;
        const auto write_result = write_frame(
            pipe,
            ipc::FrameHeader{
                .message_type = request_type,
                .payload_bytes =
                    static_cast<std::uint32_t>(payload.size()),
                .session_id = session_id,
                .request_id = request_id,
                .command_sequence = command_sequence,
            },
            payload);
        if (write_result != IoResult::ok) {
            return io_error_code(write_result);
        }

        ipc::Frame response;
        const auto read_result = read_frame(pipe, response);
        if (read_result != IoResult::ok) {
            return io_error_code(read_result);
        }
        if (!ipc::sessions_equal(
                response.header.session_id,
                session_id) ||
            response.header.request_id != request_id ||
            response.header.command_sequence != command_sequence) {
            return protocol::ErrorCode::invalid_message;
        }
        if (response.header.message_type ==
            protocol::MessageType::error) {
            protocol::ErrorCode remote_error{};
            return ipc::decode_error_payload(
                       std::span{response.payload}.first(
                           response.header.payload_bytes),
                       remote_error)
                       ? remote_error
                       : protocol::ErrorCode::invalid_message;
        }
        ipc::StatePayload state;
        const bool checkpoint_response = command.type == HookCommandType::save_checkpoint ||
            command.type == HookCommandType::load_checkpoint || command.type == HookCommandType::get_checkpoint_status;
        const auto state_bytes = checkpoint_response ? ipc::state_payload_bytes : response.header.payload_bytes;
        ipc::CheckpointPayload checkpoint_state;
        if (checkpoint_response && (response.header.payload_bytes != ipc::checkpoint_ack_payload_bytes ||
            !ipc::decode_checkpoint_payload(std::span{response.payload}.subspan(ipc::state_payload_bytes, ipc::checkpoint_payload_bytes), checkpoint_state)))
            return protocol::ErrorCode::invalid_message;
        if (response.header.message_type != response_type ||
            !ipc::decode_state_payload(
                std::span{response.payload}.first(
                    state_bytes),
                state) ||
            state.configured_speed < protocol::minimum_speed ||
            state.configured_speed > protocol::maximum_speed ||
            (!state.paused &&
             state.effective_speed != state.configured_speed) ||
            (state.paused && state.effective_speed != 0.0)) {
            return protocol::ErrorCode::hook_state_invalid;
        }

        command_sequence_ = std::max(
            command_sequence_,
            state.last_applied_command_sequence);
        output = {
            .checkpoint = checkpoint_state,
            .configured_speed = state.configured_speed,
            .effective_speed = state.effective_speed,
            .paused = state.paused,
            .accepting_commands = state.accepting_commands,
            .disconnect_recovery_occurred =
                state.disconnect_recovery_occurred,
            .command_sequence = command_sequence,
            .applied_command_sequence =
                state.last_applied_command_sequence,
            .clock_revision = state.clock_revision,
            .target_runtime = state.target_runtime,
            .time_backend = state.time_backend,
            .effect_state = state.effect_state,
        };
        return protocol::ErrorCode::ok;
    }

    [[nodiscard]] protocol::ErrorCode attach(
        UniqueHandle& pipe,
        ipc::SessionId& session_id,
        HookState& initial_state) {
        ipc::InjectorBootstrapV1 bootstrap;
        if (checkpoint_mode_) bootstrap.flags |= ipc::injector_bootstrap_checkpoint;
        bootstrap.target_process_id = target_.process_id;
        bootstrap.target_creation_time = target_.creation_time;
        bootstrap.controller_process_id = GetCurrentProcessId();
        const auto controller_creation =
            current_process_creation_time();
        if (!controller_creation.has_value() ||
            !fill_random(bootstrap.session_id) ||
            !fill_random(bootstrap.session_token)) {
            return protocol::ErrorCode::internal_error;
        }
        bootstrap.controller_creation_time =
            *controller_creation;
        const auto pipe_name =
            LR"(\\.\pipe\TimeTransle.)" +
            std::to_wstring(GetCurrentProcessId()) + L"." +
            hex_text(bootstrap.session_id);
        if (pipe_name.size() + 1 >
            bootstrap.pipe_name.size()) {
            return protocol::ErrorCode::internal_error;
        }
        std::copy(
            pipe_name.begin(),
            pipe_name.end(),
            bootstrap.pipe_name.begin());

        pipe = create_server_pipe(pipe_name);
        if (pipe.get() == INVALID_HANDLE_VALUE) {
            return protocol::ErrorCode::access_denied;
        }
        active_pipe_.store(pipe.get(), std::memory_order_release);

        const auto expected_token = bootstrap.session_token;
        const auto injector_result =
            launch_injector(bootstrap);
        if (injector_result != protocol::ErrorCode::ok) {
            return injector_result;
        }
        if (!constant_time_equal(
                expected_token,
                bootstrap.session_token)) {
            return protocol::ErrorCode::internal_error;
        }
        const auto connection = connect_pipe(pipe.get());
        if (connection != IoResult::ok) {
            return io_error_code(connection);
        }
        const auto handshake =
            perform_handshake(pipe.get(), bootstrap);
        if (handshake != protocol::ErrorCode::ok) {
            return handshake;
        }

        session_id = bootstrap.session_id;
        return transact(
            pipe.get(),
            session_id,
            HookCommand{
                .type = HookCommandType::get_checkpoint_status,
                .request = attach_request_,
            },
            initial_state);
    }

    void worker_main() noexcept {
        UniqueHandle pipe;
        ipc::SessionId session_id{};
        HookState initial_state;
        const auto attach_result =
            attach(pipe, session_id, initial_state);
        if (attach_result != protocol::ErrorCode::ok) {
            active_pipe_.store(
                INVALID_HANDLE_VALUE,
                std::memory_order_release);
            if (!should_stop()) {
                emit(HookEvent{
                    .type = HookEventType::failed,
                    .command_type = HookCommandType::get_status,
                    .request = attach_request_,
                    .error = attach_result,
                });
            }
            finish();
            return;
        }

        connected_.store(true, std::memory_order_release);
        emit(HookEvent{
            .type = HookEventType::attached,
            .command_type = HookCommandType::get_status,
            .request = attach_request_,
            .state = initial_state,
        });

        struct PendingEffect {
            HookCommand command;
            std::uint64_t command_sequence{};
            std::uint64_t clock_revision{};
            std::chrono::steady_clock::time_point started_at;
        };
        std::optional<PendingEffect> pending_effect;

        for (;;) {
            std::optional<HookCommand> command;
            {
                std::unique_lock lock{mutex_};
                if (pending_effect.has_value()) {
                    const auto elapsed = std::chrono::steady_clock::now() -
                                         pending_effect->started_at;
                    const auto interval = elapsed < unity_stalled_after
                                              ? unity_pending_poll_interval
                                              : unity_stalled_poll_interval;
                    condition_.wait_for(lock, interval, [this] {
                        return stop_requested_ || !commands_.empty();
                    });
                } else {
                    condition_.wait(lock, [this] {
                        return stop_requested_ || !commands_.empty();
                    });
                }
                if (stop_requested_) {
                    break;
                }
                if (!commands_.empty()) {
                    command = std::move(commands_.front());
                    commands_.pop_front();
                }
            }

            if (!command.has_value() && pending_effect.has_value()) {
                HookState state;
                const auto result = transact(
                    pipe.get(),
                    session_id,
                    HookCommand{
                        .type = HookCommandType::get_status,
                        .request = pending_effect->command.request,
                        .notify = false,
                    },
                    state);
                if (result != protocol::ErrorCode::ok) {
                    emit(HookEvent{
                        .type = result == protocol::ErrorCode::pipe_disconnected
                                    ? HookEventType::disconnected
                                    : HookEventType::failed,
                        .command_type = pending_effect->command.type,
                        .request = pending_effect->command.request,
                        .error = result,
                    });
                    break;
                }

                const auto effect_finished =
                    state.effect_state != protocol::EffectState::pending;
                const auto expected_revision_reached =
                    state.clock_revision >= pending_effect->clock_revision;
                const auto expected_sequence_reached =
                    state.applied_command_sequence >=
                    pending_effect->command_sequence;
                if (!effect_finished || !expected_revision_reached ||
                    (state.effect_state == protocol::EffectState::applied &&
                     !expected_sequence_reached)) {
                    continue;
                }

                state.command_sequence = pending_effect->command_sequence;
                if (pending_effect->command.notify) {
                    emit(HookEvent{
                        .type = HookEventType::command_applied,
                        .command_type = pending_effect->command.type,
                        .request = pending_effect->command.request,
                        .state = state,
                    });
                }
                const auto was_shutdown =
                    pending_effect->command.type == HookCommandType::shutdown;
                pending_effect.reset();
                if (was_shutdown) {
                    break;
                }
                continue;
            }

            if (!command.has_value()) {
                continue;
            }
            pending_effect.reset();
            HookState state;
            const auto result =
                transact(pipe.get(), session_id, *command, state);
            if (result != protocol::ErrorCode::ok) {
                emit(HookEvent{
                    .type =
                        result ==
                                protocol::ErrorCode::pipe_disconnected
                            ? HookEventType::disconnected
                            : HookEventType::failed,
                    .command_type = command->type,
                    .request = command->request,
                    .error = result,
                });
                if (static_cast<std::uint32_t>(result) >= 4100U && static_cast<std::uint32_t>(result) <= 4106U) continue;
                break;
            }
            if (command->notify) {
                emit(HookEvent{
                    .type = HookEventType::command_applied,
                    .command_type = command->type,
                    .request = command->request,
                    .state = state,
                });
            }
            if (state.time_backend == protocol::TimeBackend::unity_mono &&
                state.effect_state == protocol::EffectState::pending) {
                pending_effect = PendingEffect{
                    .command = std::move(*command),
                    .command_sequence = state.command_sequence,
                    .clock_revision = state.clock_revision,
                    .started_at = std::chrono::steady_clock::now(),
                };
                continue;
            }
            if (command->type == HookCommandType::shutdown) {
                break;
            }
        }

        connected_.store(false, std::memory_order_release);
        active_pipe_.store(
            INVALID_HANDLE_VALUE,
            std::memory_order_release);
        pipe.reset();
        finish();
    }

    void finish() noexcept {
        std::lock_guard lock{mutex_};
        connected_.store(false, std::memory_order_release);
        active_pipe_.store(
            INVALID_HANDLE_VALUE,
            std::memory_order_release);
        running_ = false;
        commands_.clear();
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<HookCommand> commands_;
    std::thread worker_;
    UniqueHandle stop_event_;
    std::atomic<HANDLE> active_pipe_{INVALID_HANDLE_VALUE};
    std::atomic<bool> connected_{};
    bool running_{};
    bool stop_requested_{};
    bool graceful_shutdown_{};
    safety::ProcessIdentity target_;
    std::filesystem::path product_directory_;
    HookRequestIdentity attach_request_;
    HookEventCallback callback_;
    bool checkpoint_mode_{};
    std::uint64_t request_id_{1};
    std::uint64_t command_sequence_{};
};

HookSession::HookSession()
    : implementation_{std::make_unique<Impl>()} {}

HookSession::~HookSession() = default;

bool HookSession::start(
    const safety::ProcessIdentity& target,
    std::filesystem::path product_directory,
    HookRequestIdentity attach_request,
    HookEventCallback callback, const bool checkpoint_mode) {
    return implementation_->start(
        target,
        std::move(product_directory),
        std::move(attach_request),
        std::move(callback), checkpoint_mode);
}

bool HookSession::submit(HookCommand command) {
    return implementation_->submit(std::move(command));
}

void HookSession::stop(const bool graceful) noexcept {
    implementation_->stop(graceful);
}

bool HookSession::connected() const noexcept {
    return implementation_->connected();
}

}  // namespace timetransle::controller
