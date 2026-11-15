#include "timetransle/hook/api.hpp"

#include <MinHook.h>
#include <mmsystem.h>
#include <process.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <optional>
#include <span>
#include <string_view>

#include "timetransle/clock/virtual_clock.hpp"
#include "timetransle/common/build_info.hpp"
#include "timetransle/hook/hook_installer.hpp"
#include "timetransle/hook/unity_mono_adapter.hpp"
#include "timetransle/checkpoint/runtime.hpp"
#include "timetransle/ipc/pipe_io.hpp"
#include "timetransle/ipc/protocol.hpp"
#include "timetransle/protocol/error_code.hpp"

namespace timetransle::hook {
namespace {

using QueryPerformanceCounterFunction = BOOL(WINAPI*)(LARGE_INTEGER*);
using GetTickCountFunction = DWORD(WINAPI*)();
using GetTickCount64Function = ULONGLONG(WINAPI*)();
using TimeGetTimeFunction = DWORD(WINAPI*)();

class ExclusiveControlLock final {
public:
    explicit ExclusiveControlLock(SRWLOCK& lock) noexcept : lock_(lock) {
        AcquireSRWLockExclusive(&lock_);
    }

    ~ExclusiveControlLock() {
        ReleaseSRWLockExclusive(&lock_);
    }

    ExclusiveControlLock(const ExclusiveControlLock&) = delete;
    ExclusiveControlLock& operator=(const ExclusiveControlLock&) = delete;

private:
    SRWLOCK& lock_;
};

class DetourCallGuard final {
public:
    explicit DetourCallGuard(std::atomic<std::uint32_t>& count) noexcept : count_(count) {
        count_.fetch_add(1, std::memory_order_acquire);
    }

    ~DetourCallGuard() {
        count_.fetch_sub(1, std::memory_order_release);
    }

    DetourCallGuard(const DetourCallGuard&) = delete;
    DetourCallGuard& operator=(const DetourCallGuard&) = delete;

private:
    std::atomic<std::uint32_t>& count_;
};

struct InstallationContext {
    clock::ClockOrigins origins{};
    void* original_qpc{};
    void* original_tick32{};
    void* original_tick64{};
    void* original_time_get_time{};
    void* original_unity_get_time_scale{};
    void* original_unity_set_time_scale{};
    void* original_unity_get_delta_time{};
    bool install_unity_backend{};
};

struct IpcBootstrap {
    std::uint32_t controller_process_id{};
    std::uint64_t controller_creation_time{};
    ipc::SessionId session_id{};
    ipc::SessionToken session_token{};
    std::array<wchar_t, maximum_pipe_name_characters> pipe_name{};
};

SRWLOCK control_lock = SRWLOCK_INIT;
std::atomic<bool> initialized{};
std::atomic<bool> accepting_commands{};
std::atomic<std::uint32_t> detours_in_flight{};
std::atomic<clock::VirtualClock*> published_clock{};
std::atomic<IpcRuntimeState> ipc_runtime_state{IpcRuntimeState::disabled};
std::atomic<std::uint64_t> last_applied_command_sequence{};
std::atomic<std::uint64_t> last_accepted_command_sequence{};
std::atomic<bool> disconnect_recovery_occurred{};
std::atomic<bool> shutdown_requested{};
std::optional<clock::VirtualClock> clock_storage;
IpcBootstrap ipc_bootstrap;
UnityMonoAdapter unity_adapter;

QueryPerformanceCounterFunction original_qpc{};
GetTickCountFunction original_tick32{};
GetTickCount64Function original_tick64{};
TimeGetTimeFunction original_time_get_time{};

[[nodiscard]] bool minhook_initialize(void*) noexcept {
    return MH_Initialize() == MH_OK;
}

[[nodiscard]] bool minhook_create(
    void*,
    void* const target,
    void* const detour,
    void** const original) noexcept {
    return MH_CreateHook(target, detour, original) == MH_OK;
}

[[nodiscard]] bool minhook_queue_enable(
    void*,
    void* const target) noexcept {
    return MH_QueueEnableHook(target) == MH_OK;
}

[[nodiscard]] bool minhook_apply_queued(void*) noexcept {
    return MH_ApplyQueued() == MH_OK;
}

void minhook_disable_all(void*) noexcept {
    const auto status = MH_DisableHook(MH_ALL_HOOKS);
    static_cast<void>(status);
}

void minhook_remove(void*, void* const target) noexcept {
    const auto status = MH_RemoveHook(target);
    static_cast<void>(status);
}

void minhook_uninitialize(void*) noexcept {
    const auto status = MH_Uninitialize();
    static_cast<void>(status);
}

[[nodiscard]] HookBackend minhook_backend() noexcept {
    return {
        .context = nullptr,
        .initialize = &minhook_initialize,
        .create = &minhook_create,
        .queue_enable = &minhook_queue_enable,
        .apply_queued = &minhook_apply_queued,
        .disable_all = &minhook_disable_all,
        .remove = &minhook_remove,
        .uninitialize = &minhook_uninitialize,
    };
}

HookInstaller hook_installer{minhook_backend()};

[[nodiscard]] std::int64_t read_original_qpc(void*) noexcept {
    LARGE_INTEGER value{};
    if (original_qpc != nullptr && original_qpc(&value) != FALSE) {
        return value.QuadPart;
    }

    return 0;
}

[[nodiscard]] BOOL WINAPI detour_query_performance_counter(
    LARGE_INTEGER* const output) {
    DetourCallGuard guard{detours_in_flight};
    if (unity_adapter.uses_unity_backend()) {
        return original_qpc(output);
    }
    const auto* const active_clock = published_clock.load(std::memory_order_acquire);
    if (active_clock == nullptr || output == nullptr) {
        return original_qpc(output);
    }

    output->QuadPart = active_clock->read_qpc();
    return TRUE;
}

[[nodiscard]] DWORD WINAPI detour_get_tick_count() {
    DetourCallGuard guard{detours_in_flight};
    if (unity_adapter.uses_unity_backend()) {
        return original_tick32();
    }
    const auto* const active_clock = published_clock.load(std::memory_order_acquire);
    if (active_clock == nullptr) {
        return original_tick32();
    }

    return active_clock->read_tick_count32();
}

[[nodiscard]] ULONGLONG WINAPI detour_get_tick_count64() {
    DetourCallGuard guard{detours_in_flight};
    if (unity_adapter.uses_unity_backend()) {
        return original_tick64();
    }
    const auto* const active_clock = published_clock.load(std::memory_order_acquire);
    if (active_clock == nullptr) {
        return original_tick64();
    }

    return active_clock->read_tick_count64();
}

[[nodiscard]] DWORD WINAPI detour_time_get_time() {
    DetourCallGuard guard{detours_in_flight};
    if (unity_adapter.uses_unity_backend()) {
        return original_time_get_time();
    }
    const auto* const active_clock = published_clock.load(std::memory_order_acquire);
    if (active_clock == nullptr) {
        return original_time_get_time();
    }

    return active_clock->read_time_get_time();
}

[[nodiscard]] float __cdecl detour_unity_get_time_scale() {
    DetourCallGuard guard{detours_in_flight};
    return unity_adapter.get_time_scale();
}

void __cdecl detour_unity_set_time_scale(const float value) {
    DetourCallGuard guard{detours_in_flight};
    unity_adapter.set_time_scale(value);
}

[[nodiscard]] float __cdecl detour_unity_get_delta_time() {
    DetourCallGuard guard{detours_in_flight};
    return unity_adapter.get_delta_time();
}

[[nodiscard]] void* resolve_export(
    const HMODULE module,
    const char* const name) noexcept {
    return reinterpret_cast<void*>(GetProcAddress(module, name));
}

[[nodiscard]] bool capture_origins(InstallationContext& context) noexcept {
    LARGE_INTEGER qpc{};
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) == FALSE || frequency.QuadPart <= 0 ||
        QueryPerformanceCounter(&qpc) == FALSE) {
        return false;
    }

    context.origins = {
        .qpc = qpc.QuadPart,
        .qpc_frequency = static_cast<std::uint64_t>(frequency.QuadPart),
        .tick_count64 = GetTickCount64(),
        .tick_count32 = GetTickCount(),
        .time_get_time = timeGetTime(),
    };
    return true;
}

[[nodiscard]] bool prepare_clock(void* const raw_context) noexcept {
    auto& context = *static_cast<InstallationContext*>(raw_context);
    original_qpc =
        reinterpret_cast<QueryPerformanceCounterFunction>(context.original_qpc);
    original_tick32 =
        reinterpret_cast<GetTickCountFunction>(context.original_tick32);
    original_tick64 =
        reinterpret_cast<GetTickCount64Function>(context.original_tick64);
    original_time_get_time =
        reinterpret_cast<TimeGetTimeFunction>(context.original_time_get_time);

    if (original_qpc == nullptr || original_tick32 == nullptr ||
        original_tick64 == nullptr || original_time_get_time == nullptr) {
        return false;
    }

    clock_storage.emplace(
        context.origins,
        clock::RealQpcSource{
            .read = &read_original_qpc,
            .context = nullptr,
        });
    if (!clock_storage->is_ready()) {
        return false;
    }

    if (!context.install_unity_backend) {
        return true;
    }
    return unity_adapter.activate(
        reinterpret_cast<UnityTimeGetter>(context.original_unity_get_time_scale),
        reinterpret_cast<UnityTimeSetter>(context.original_unity_set_time_scale),
        reinterpret_cast<UnityTimeGetter>(context.original_unity_get_delta_time),
        clock_storage->state().revision);
}

[[nodiscard]] HookApiResult map_update_result(
    const clock::UpdateResult result) noexcept {
    switch (result) {
        case clock::UpdateResult::applied:
            return HookApiResult::ok;
        case clock::UpdateResult::invalid_speed:
            return HookApiResult::invalid_speed;
        case clock::UpdateResult::not_initialized:
            return HookApiResult::clock_initialization_failed;
    }

    return HookApiResult::clock_initialization_failed;
}

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

[[nodiscard]] constexpr std::uint64_t file_time_to_uint64(
    const FILETIME value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           value.dwLowDateTime;
}

[[nodiscard]] bool verify_pipe_server(
    const HANDLE pipe,
    const IpcBootstrap& bootstrap) noexcept {
    ULONG server_process_id{};
    if (GetNamedPipeServerProcessId(pipe, &server_process_id) == FALSE ||
        server_process_id != bootstrap.controller_process_id) {
        return false;
    }

    UniqueHandle process{OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        bootstrap.controller_process_id)};
    if (process.get() == nullptr) {
        return false;
    }

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    return GetProcessTimes(
               process.get(),
               &creation,
               &exit,
               &kernel,
               &user) != FALSE &&
           file_time_to_uint64(creation) == bootstrap.controller_creation_time;
}

[[nodiscard]] bool tokens_equal(
    const ipc::SessionToken& left,
    const ipc::SessionToken& right) noexcept {
    std::byte difference{};
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == std::byte{0};
}

[[nodiscard]] ipc::StatePayload current_ipc_state_payload() noexcept {
    const auto clock_state = clock_storage->state();
    const auto unity_state = unity_adapter.snapshot();
    const auto applied_sequence =
        unity_state.time_backend == protocol::TimeBackend::unity_mono
            ? unity_state.applied_command_sequence
            : last_applied_command_sequence.load(std::memory_order_acquire);
    return {
        .configured_speed = clock_state.configured_speed,
        .effective_speed =
            clock_state.paused ? 0.0 : clock_state.configured_speed,
        .clock_revision = clock_state.revision,
        .last_applied_command_sequence =
            applied_sequence,
        .paused = clock_state.paused,
        .accepting_commands =
            accepting_commands.load(std::memory_order_acquire),
        .disconnect_recovery_occurred =
            disconnect_recovery_occurred.load(std::memory_order_acquire),
        .target_runtime = unity_state.target_runtime,
        .time_backend = unity_state.time_backend,
        .effect_state = unity_state.effect_state,
    };
}

void queue_current_state_for_backend(const std::uint64_t command_sequence) noexcept {
    const auto clock_state = clock_storage->state();
    if (unity_adapter.uses_unity_backend()) {
        unity_adapter.queue_external_state(
            clock_state.configured_speed,
            clock_state.paused,
            clock_state.revision,
            command_sequence);
        return;
    }
    last_applied_command_sequence.store(command_sequence, std::memory_order_release);
}

[[nodiscard]] bool send_state_response(
    const HANDLE pipe,
    const protocol::MessageType message_type,
    const ipc::FrameHeader& request) noexcept {
    std::array<std::byte, ipc::state_payload_bytes> payload{};
    ipc::encode_state_payload(current_ipc_state_payload(), payload);
    return ipc::write_frame(
               pipe,
               ipc::FrameHeader{
                   .message_type = message_type,
                   .payload_bytes = static_cast<std::uint32_t>(payload.size()),
                   .session_id = ipc_bootstrap.session_id,
                   .request_id = request.request_id,
                   .command_sequence = request.command_sequence,
               },
               payload) == ipc::PipeIoResult::ok;
}

[[nodiscard]] bool send_error_response(
    const HANDLE pipe,
    const ipc::FrameHeader& request,
    const protocol::ErrorCode error) noexcept {
    std::array<std::byte, ipc::error_payload_bytes> payload{};
    ipc::encode_error_payload(error, payload);
    return ipc::write_frame(
               pipe,
               ipc::FrameHeader{
                   .message_type = protocol::MessageType::error,
                   .payload_bytes = static_cast<std::uint32_t>(payload.size()),
                   .session_id = ipc_bootstrap.session_id,
                   .request_id = request.request_id,
                   .command_sequence = request.command_sequence,
               },
               payload) == ipc::PipeIoResult::ok;
}

[[nodiscard]] bool send_checkpoint_response(HANDLE pipe, protocol::MessageType type, const ipc::FrameHeader& request) noexcept {
    std::array<std::byte, ipc::checkpoint_ack_payload_bytes> bytes{};
    ipc::encode_state_payload(current_ipc_state_payload(), std::span<std::byte, ipc::state_payload_bytes>{bytes.data(), ipc::state_payload_bytes});
    ipc::encode_checkpoint_payload(checkpoint::state(), std::span{bytes}.subspan(ipc::state_payload_bytes));
    return ipc::write_frame(pipe, ipc::FrameHeader{.message_type=type,
        .payload_bytes=static_cast<std::uint32_t>(bytes.size()), .session_id=ipc_bootstrap.session_id,
        .request_id=request.request_id, .command_sequence=request.command_sequence}, bytes) == ipc::PipeIoResult::ok;
}

enum class CommandResult {
    continue_session,
    shutdown,
    failed,
};

[[nodiscard]] CommandResult handle_ipc_command(
    const HANDLE pipe,
    const ipc::Frame& frame) noexcept {
    if (!ipc::sessions_equal(frame.header.session_id, ipc_bootstrap.session_id)) {
        const auto sent = send_error_response(
            pipe,
            frame.header,
            protocol::ErrorCode::session_mismatch);
        static_cast<void>(sent);
        return CommandResult::failed;
    }

    const auto payload =
        std::span{frame.payload}.first(frame.header.payload_bytes);
    if (frame.header.message_type == protocol::MessageType::get_status) {
        if (!payload.empty()) {
            return send_error_response(
                       pipe,
                       frame.header,
                       protocol::ErrorCode::invalid_message)
                       ? CommandResult::continue_session
                       : CommandResult::failed;
        }
        if (!send_state_response(pipe, protocol::MessageType::status, frame.header)) {
            return CommandResult::failed;
        }
        if (shutdown_requested.load(std::memory_order_acquire) &&
            unity_adapter.snapshot().effect_state != protocol::EffectState::pending) {
            return CommandResult::shutdown;
        }
        return CommandResult::continue_session;
    }
    if (frame.header.message_type == protocol::MessageType::ping) {
        if (!payload.empty()) {
            return CommandResult::failed;
        }
        return ipc::write_frame(
                   pipe,
                   ipc::FrameHeader{
                       .message_type = protocol::MessageType::pong,
                       .payload_bytes = 0,
                       .session_id = ipc_bootstrap.session_id,
                       .request_id = frame.header.request_id,
                       .command_sequence = frame.header.command_sequence,
                   },
                   std::span<const std::byte>{}) == ipc::PipeIoResult::ok
                   ? CommandResult::continue_session
                   : CommandResult::failed;
    }

    const auto kind = frame.header.message_type;
    if (kind == protocol::MessageType::get_checkpoint_status ||
        kind == protocol::MessageType::save_checkpoint || kind == protocol::MessageType::load_checkpoint) {
        if (!payload.empty()) return send_error_response(pipe, frame.header, protocol::ErrorCode::invalid_message)
            ? CommandResult::continue_session : CommandResult::failed;
        auto type = protocol::MessageType::checkpoint_status;
        if (kind != protocol::MessageType::get_checkpoint_status) {
            if (frame.header.command_sequence <= last_accepted_command_sequence.load())
                return send_error_response(pipe, frame.header, protocol::ErrorCode::stale_sequence)
                    ? CommandResult::continue_session : CommandResult::failed;
            const auto result = checkpoint::execute(kind == protocol::MessageType::load_checkpoint);
            if (result != protocol::ErrorCode::ok)
                return send_error_response(pipe, frame.header, result) ? CommandResult::continue_session : CommandResult::failed;
            last_accepted_command_sequence.store(frame.header.command_sequence);
            queue_current_state_for_backend(frame.header.command_sequence);
            type = kind == protocol::MessageType::save_checkpoint ?
                protocol::MessageType::save_checkpoint_ack : protocol::MessageType::load_checkpoint_ack;
        }
        return send_checkpoint_response(pipe, type, frame.header) ? CommandResult::continue_session : CommandResult::failed;
    }

    protocol::MessageType response_type{};
    const auto is_state_command =
        frame.header.message_type == protocol::MessageType::set_speed ||
        frame.header.message_type == protocol::MessageType::pause ||
        frame.header.message_type == protocol::MessageType::resume ||
        frame.header.message_type == protocol::MessageType::reset ||
        frame.header.message_type == protocol::MessageType::shutdown;
    if (!is_state_command) {
        const auto sent = send_error_response(
            pipe,
            frame.header,
            protocol::ErrorCode::invalid_message);
        static_cast<void>(sent);
        return CommandResult::failed;
    }

    const auto previous_sequence =
        last_accepted_command_sequence.load(std::memory_order_acquire);
    if (frame.header.command_sequence == 0 ||
        frame.header.command_sequence <= previous_sequence) {
        return send_error_response(
                   pipe,
                   frame.header,
                   protocol::ErrorCode::stale_sequence)
                   ? CommandResult::continue_session
                   : CommandResult::failed;
    }

    HookApiResult update_result{HookApiResult::invalid_argument};
    {
        ExclusiveControlLock lock{control_lock};
        if (!accepting_commands.load(std::memory_order_acquire)) {
            return CommandResult::failed;
        }

        switch (frame.header.message_type) {
            case protocol::MessageType::set_speed: {
                double speed{};
                if (!ipc::decode_speed_payload(payload, speed)) {
                    update_result = HookApiResult::invalid_argument;
                } else {
                    update_result =
                        map_update_result(clock_storage->set_speed(speed));
                }
                response_type = protocol::MessageType::set_speed_ack;
                break;
            }
            case protocol::MessageType::pause:
                if (payload.empty()) {
                    update_result =
                        map_update_result(clock_storage->set_paused(true));
                }
                response_type = protocol::MessageType::pause_ack;
                break;
            case protocol::MessageType::resume:
                if (payload.empty()) {
                    update_result =
                        map_update_result(clock_storage->set_paused(false));
                }
                response_type = protocol::MessageType::resume_ack;
                break;
            case protocol::MessageType::reset:
                if (payload.empty()) {
                    update_result = map_update_result(clock_storage->reset());
                }
                response_type = protocol::MessageType::reset_ack;
                break;
            case protocol::MessageType::shutdown:
                if (payload.empty()) {
                    update_result = map_update_result(clock_storage->reset());
                }
                response_type = protocol::MessageType::shutdown_ack;
                break;
            default:
                break;
        }

        if (update_result == HookApiResult::ok) {
            last_accepted_command_sequence.store(
                frame.header.command_sequence,
                std::memory_order_release);
            queue_current_state_for_backend(frame.header.command_sequence);
            if (frame.header.message_type == protocol::MessageType::shutdown) {
                accepting_commands.store(false, std::memory_order_release);
                shutdown_requested.store(true, std::memory_order_release);
            }
        }
    }

    if (update_result != HookApiResult::ok) {
        const auto error = update_result == HookApiResult::invalid_speed
                               ? protocol::ErrorCode::invalid_speed
                               : protocol::ErrorCode::invalid_message;
        return send_error_response(pipe, frame.header, error)
                   ? CommandResult::continue_session
                   : CommandResult::failed;
    }

    if (!send_state_response(pipe, response_type, frame.header)) {
        return CommandResult::failed;
    }
    if (frame.header.message_type == protocol::MessageType::shutdown &&
        unity_adapter.snapshot().effect_state != protocol::EffectState::pending) {
        return CommandResult::shutdown;
    }
    return CommandResult::continue_session;
}

void recover_from_disconnect() noexcept {
    checkpoint::clear();
    ExclusiveControlLock lock{control_lock};
    if (clock_storage.has_value()) {
        const auto result = clock_storage->reset();
        static_cast<void>(result);
        if (result == clock::UpdateResult::applied) {
            queue_current_state_for_backend(
                last_accepted_command_sequence.load(std::memory_order_acquire));
        }
    }
    accepting_commands.store(false, std::memory_order_release);
    disconnect_recovery_occurred.store(true, std::memory_order_release);
    ipc_runtime_state.store(IpcRuntimeState::disconnected, std::memory_order_release);
}

[[nodiscard]] bool perform_handshake(const HANDLE pipe) noexcept {
    std::array<std::byte, ipc::hello_payload_bytes> hello_bytes{};
    ipc::encode_hello_payload(
        ipc::HelloPayload{
            .token = ipc_bootstrap.session_token,
            .process_id = GetCurrentProcessId(),
        },
        hello_bytes);
    const ipc::FrameHeader hello_header{
        .message_type = protocol::MessageType::hello,
        .payload_bytes = static_cast<std::uint32_t>(hello_bytes.size()),
        .session_id = ipc_bootstrap.session_id,
        .request_id = 1,
        .command_sequence = 0,
    };
    if (ipc::write_frame(pipe, hello_header, hello_bytes) != ipc::PipeIoResult::ok) {
        return false;
    }

    ipc::Frame response;
    if (ipc::read_frame(pipe, response) != ipc::PipeIoResult::ok ||
        response.header.message_type != protocol::MessageType::hello_ack ||
        response.header.request_id != hello_header.request_id ||
        !ipc::sessions_equal(response.header.session_id, ipc_bootstrap.session_id)) {
        return false;
    }

    ipc::HelloPayload acknowledgement;
    return ipc::decode_hello_payload(
               std::span{response.payload}.first(response.header.payload_bytes),
               acknowledgement) &&
           acknowledgement.process_id == ipc_bootstrap.controller_process_id &&
           tokens_equal(acknowledgement.token, ipc_bootstrap.session_token);
}

unsigned __stdcall ipc_thread_main(void*) noexcept {
    if (WaitNamedPipeW(ipc_bootstrap.pipe_name.data(), 5'000) == FALSE) {
        recover_from_disconnect();
        return 0;
    }

    UniqueHandle pipe{CreateFileW(
        ipc_bootstrap.pipe_name.data(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr)};
    if (pipe.get() == INVALID_HANDLE_VALUE ||
        !verify_pipe_server(pipe.get(), ipc_bootstrap) ||
        !perform_handshake(pipe.get())) {
        recover_from_disconnect();
        return 0;
    }

    accepting_commands.store(true, std::memory_order_release);
    ipc_runtime_state.store(IpcRuntimeState::active, std::memory_order_release);

    for (;;) {
        ipc::Frame frame;
        if (ipc::read_frame(pipe.get(), frame) != ipc::PipeIoResult::ok) {
            recover_from_disconnect();
            return 0;
        }

        const auto command_result = handle_ipc_command(pipe.get(), frame);
        if (command_result == CommandResult::failed) {
            recover_from_disconnect();
            return 0;
        }
        if (command_result == CommandResult::shutdown) {
            ipc_runtime_state.store(IpcRuntimeState::shutdown, std::memory_order_release);
            return 0;
        }
    }
}

[[nodiscard]] bool start_ipc_thread(
    const InitializeRequestV1& request) noexcept {
    ipc_bootstrap = {
        .controller_process_id = request.controller_process_id,
        .controller_creation_time = request.controller_creation_time,
        .session_id = request.session_id,
        .session_token = request.session_token,
        .pipe_name = request.pipe_name,
    };
    ipc_runtime_state.store(IpcRuntimeState::connecting, std::memory_order_release);

    const auto thread = reinterpret_cast<HANDLE>(_beginthreadex(
        nullptr,
        0,
        &ipc_thread_main,
        nullptr,
        0,
        nullptr));
    if (thread == nullptr) {
        ipc_runtime_state.store(
            IpcRuntimeState::disconnected,
            std::memory_order_release);
        return false;
    }

    CloseHandle(thread);
    return true;
}

[[nodiscard]] bool request_is_valid(
    const InitializeRequestV1& request) noexcept {
    if (request.struct_size != sizeof(InitializeRequestV1) ||
        request.magic != local_bootstrap_magic ||
        request.version != local_bootstrap_version ||
        request.process_id != GetCurrentProcessId() ||
        (request.flags & product_bootstrap_flag) == 0U ||
        (request.flags & ~(product_bootstrap_flag | enable_ipc_flag | enable_checkpoint_flag)) != 0U) {
        return false;
    }

    if ((request.flags & enable_ipc_flag) == 0U) {
        return true;
    }

    const auto terminator = std::find(
        request.pipe_name.begin(),
        request.pipe_name.end(),
        L'\0');
    constexpr std::wstring_view pipe_prefix{LR"(\\.\pipe\)"};
    return request.controller_process_id != 0U &&
           request.controller_creation_time != 0U &&
           terminator != request.pipe_name.begin() &&
           terminator != request.pipe_name.end() &&
           std::wstring_view{
               request.pipe_name.data(),
               static_cast<std::size_t>(terminator - request.pipe_name.begin())}
               .starts_with(pipe_prefix);
}

}  // namespace
}  // namespace timetransle::hook

extern "C" {

timetransle::hook::HookApiResult WINAPI TimeTransleInitialize(
    const timetransle::hook::InitializeRequestV1* const request) noexcept {
    using namespace timetransle::hook;

    if (request == nullptr || !request_is_valid(*request)) {
        return HookApiResult::invalid_argument;
    }

    ExclusiveControlLock lock{control_lock};
    if (initialized.load(std::memory_order_acquire)) {
        return HookApiResult::already_initialized;
    }

    InstallationContext context;
    unity_adapter.reset();
    if (!capture_origins(context)) {
        return HookApiResult::clock_initialization_failed;
    }

    const auto kernel32 = GetModuleHandleW(L"kernel32.dll");
    const auto winmm = GetModuleHandleW(L"winmm.dll");
    if (kernel32 == nullptr || winmm == nullptr) {
        return HookApiResult::hook_setup_failed;
    }

    const auto qpc_target = resolve_export(kernel32, "QueryPerformanceCounter");
    const auto tick32_target = resolve_export(kernel32, "GetTickCount");
    const auto tick64_target = resolve_export(kernel32, "GetTickCount64");
    const auto time_get_time_target = resolve_export(winmm, "timeGetTime");

    const std::array generic_hooks{
        HookSpec{
            qpc_target,
            reinterpret_cast<void*>(&detour_query_performance_counter),
            &context.original_qpc,
        },
        HookSpec{
            tick32_target,
            reinterpret_cast<void*>(&detour_get_tick_count),
            &context.original_tick32,
        },
        HookSpec{
            tick64_target,
            reinterpret_cast<void*>(&detour_get_tick_count64),
            &context.original_tick64,
        },
        HookSpec{
            time_get_time_target,
            reinterpret_cast<void*>(&detour_time_get_time),
            &context.original_time_get_time,
        },
    };

    clock_storage.reset();
    const auto unity_discovery = discover_unity_mono();
    auto install_result = InstallResult::invalid_spec;
    if (unity_discovery.unity_time_found) {
        context.install_unity_backend = true;
        const std::array unity_hooks{
            generic_hooks[0],
            generic_hooks[1],
            generic_hooks[2],
            generic_hooks[3],
            HookSpec{
                unity_discovery.get_time_scale,
                reinterpret_cast<void*>(&detour_unity_get_time_scale),
                &context.original_unity_get_time_scale,
            },
            HookSpec{
                unity_discovery.set_time_scale,
                reinterpret_cast<void*>(&detour_unity_set_time_scale),
                &context.original_unity_set_time_scale,
            },
            HookSpec{
                unity_discovery.get_delta_time,
                reinterpret_cast<void*>(&detour_unity_get_delta_time),
                &context.original_unity_get_delta_time,
            },
        };
        install_result = hook_installer.install(unity_hooks, &prepare_clock, &context);
        if (install_result != InstallResult::installed) {
            clock_storage.reset();
            unity_adapter.reset();
            context.install_unity_backend = false;
            install_result = hook_installer.install(generic_hooks, &prepare_clock, &context);
            if (install_result == InstallResult::installed) {
                unity_adapter.mark_unity_detected_degraded();
            }
        }
    } else {
        install_result = hook_installer.install(generic_hooks, &prepare_clock, &context);
    }
    if (install_result != InstallResult::installed || !clock_storage.has_value()) {
        clock_storage.reset();
        return install_result == InstallResult::prepare_failed
                   ? HookApiResult::clock_initialization_failed
                   : HookApiResult::hook_setup_failed;
    }

    published_clock.store(&*clock_storage, std::memory_order_release);
    last_applied_command_sequence.store(0, std::memory_order_release);
    last_accepted_command_sequence.store(0, std::memory_order_release);
    disconnect_recovery_occurred.store(false, std::memory_order_release);
    shutdown_requested.store(false, std::memory_order_release);
    if ((request->flags & enable_checkpoint_flag) != 0U) {
        if (!timetransle::checkpoint::initialize(&*clock_storage)) return HookApiResult::hook_setup_failed;
    }
    initialized.store(true, std::memory_order_release);

    if ((request->flags & enable_ipc_flag) != 0U) {
        accepting_commands.store(false, std::memory_order_release);
        if (!start_ipc_thread(*request)) {
            recover_from_disconnect();
            return HookApiResult::hook_setup_failed;
        }
    } else {
        ipc_runtime_state.store(IpcRuntimeState::disabled, std::memory_order_release);
        accepting_commands.store(true, std::memory_order_release);
    }

    return HookApiResult::ok;
}

timetransle::hook::HookApiResult WINAPI TimeTransleSetSpeed(
    const double speed) noexcept {
    using namespace timetransle::hook;

    ExclusiveControlLock lock{control_lock};
    if (!initialized.load(std::memory_order_acquire)) {
        return HookApiResult::not_initialized;
    }
    if (!accepting_commands.load(std::memory_order_acquire)) {
        return HookApiResult::inactive;
    }

    const auto result = map_update_result(clock_storage->set_speed(speed));
    if (result == HookApiResult::ok) {
        queue_current_state_for_backend(
            last_accepted_command_sequence.load(std::memory_order_acquire));
    }
    return result;
}

DWORD WINAPI TimeTransleInitializeThread(
    void* const request) noexcept {
    return static_cast<DWORD>(TimeTransleInitialize(
        static_cast<
            const timetransle::hook::InitializeRequestV1*>(
            request)));
}

timetransle::hook::HookApiResult WINAPI TimeTransleSetPaused(
    const std::uint32_t paused) noexcept {
    using namespace timetransle::hook;

    if (paused > 1U) {
        return HookApiResult::invalid_argument;
    }

    ExclusiveControlLock lock{control_lock};
    if (!initialized.load(std::memory_order_acquire)) {
        return HookApiResult::not_initialized;
    }
    if (!accepting_commands.load(std::memory_order_acquire)) {
        return HookApiResult::inactive;
    }

    const auto result = map_update_result(clock_storage->set_paused(paused != 0U));
    if (result == HookApiResult::ok) {
        queue_current_state_for_backend(
            last_accepted_command_sequence.load(std::memory_order_acquire));
    }
    return result;
}

timetransle::hook::HookApiResult WINAPI TimeTransleReset() noexcept {
    using namespace timetransle::hook;

    ExclusiveControlLock lock{control_lock};
    if (!initialized.load(std::memory_order_acquire)) {
        return HookApiResult::not_initialized;
    }
    if (!accepting_commands.load(std::memory_order_acquire)) {
        return HookApiResult::inactive;
    }

    const auto result = map_update_result(clock_storage->reset());
    if (result == HookApiResult::ok) {
        queue_current_state_for_backend(
            last_accepted_command_sequence.load(std::memory_order_acquire));
    }
    return result;
}

timetransle::hook::HookApiResult WINAPI TimeTransleDeactivate() noexcept {
    using namespace timetransle::hook;

    ExclusiveControlLock lock{control_lock};
    if (!initialized.load(std::memory_order_acquire)) {
        return HookApiResult::not_initialized;
    }
    if (!accepting_commands.load(std::memory_order_acquire)) {
        return HookApiResult::inactive;
    }

    const auto result = map_update_result(clock_storage->reset());
    if (result == HookApiResult::ok) {
        queue_current_state_for_backend(
            last_accepted_command_sequence.load(std::memory_order_acquire));
        accepting_commands.store(false, std::memory_order_release);
        ipc_runtime_state.store(IpcRuntimeState::shutdown, std::memory_order_release);
    }
    return result;
}

timetransle::hook::HookApiResult WINAPI TimeTransleGetState(
    timetransle::hook::RuntimeStateV1* const state) noexcept {
    using namespace timetransle::hook;

    if (state == nullptr || state->struct_size != sizeof(RuntimeStateV1)) {
        return HookApiResult::invalid_argument;
    }

    const auto* const active_clock = published_clock.load(std::memory_order_acquire);
    if (active_clock == nullptr) {
        return HookApiResult::not_initialized;
    }

    const auto clock_state = active_clock->state();
    *state = {
        .struct_size = sizeof(RuntimeStateV1),
        .configured_speed = clock_state.configured_speed,
        .paused = clock_state.paused ? 1U : 0U,
        .accepting_commands =
            accepting_commands.load(std::memory_order_acquire) ? 1U : 0U,
        .clock_revision = clock_state.revision,
        .detours_in_flight = detours_in_flight.load(std::memory_order_acquire),
        .ipc_state = ipc_runtime_state.load(std::memory_order_acquire),
        .last_applied_command_sequence =
            last_applied_command_sequence.load(std::memory_order_acquire),
        .disconnect_recovery_occurred =
            disconnect_recovery_occurred.load(std::memory_order_acquire) ? 1U : 0U,
    };
    return HookApiResult::ok;
}

timetransle::hook::HookApiResult WINAPI TimeTransleGetBuildId(
    char* const output,
    const std::uint32_t capacity) noexcept {
    using namespace timetransle;

    if (output == nullptr ||
        capacity <= build_info::build_id.size()) {
        return hook::HookApiResult::invalid_argument;
    }

    std::copy(
        build_info::build_id.begin(),
        build_info::build_id.end(),
        output);
    output[build_info::build_id.size()] = '\0';
    return hook::HookApiResult::ok;
}

std::uint32_t WINAPI TimeTransleGetApiVersion() noexcept {
    return timetransle::hook::local_bootstrap_version;
}

}
