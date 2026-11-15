#include <Windows.h>
#include <bcrypt.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "timetransle/hook/api.hpp"

namespace {

int controller_e2e_failure_code = 1;

using timetransle::hook::DeactivateFunction;
using timetransle::hook::GetStateFunction;
using timetransle::hook::HookApiResult;
using timetransle::hook::InitializeFunction;
using timetransle::hook::InitializeRequestV1;
using timetransle::hook::IpcRuntimeState;
using timetransle::hook::ResetFunction;
using timetransle::hook::RuntimeStateV1;
using timetransle::hook::SetPausedFunction;
using timetransle::hook::SetSpeedFunction;

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

template <typename Function>
[[nodiscard]] Function resolve(
    const HMODULE module,
    const char* const name) {
    const auto address = GetProcAddress(module, name);
    if (address == nullptr) {
        throw TestFailure(std::string{"missing Hook export: "} + name);
    }
    return reinterpret_cast<Function>(address);
}

struct HookApi {
    InitializeFunction initialize{};
    SetSpeedFunction set_speed{};
    SetPausedFunction set_paused{};
    ResetFunction reset{};
    DeactivateFunction deactivate{};
    GetStateFunction get_state{};
};

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

struct ControllerProcess {
    UniqueHandle process;
    UniqueHandle thread;
    std::uint32_t process_id{};
    std::uint64_t creation_time{};
};

struct TimeSample {
    std::int64_t qpc{};
    std::uint64_t tick64{};
    std::uint32_t tick32{};
    std::uint32_t multimedia{};
};

struct MeasuredInterval {
    double reality_milliseconds{};
    double qpc_milliseconds{};
    double tick64_milliseconds{};
    double tick32_milliseconds{};
    double multimedia_milliseconds{};
};

[[nodiscard]] TimeSample sample_time() {
    LARGE_INTEGER qpc{};
    REQUIRE(QueryPerformanceCounter(&qpc) != FALSE);
    return {
        .qpc = qpc.QuadPart,
        .tick64 = GetTickCount64(),
        .tick32 = GetTickCount(),
        .multimedia = timeGetTime(),
    };
}

[[nodiscard]] std::uint64_t unbiased_time_100ns() {
    ULONGLONG value{};
    REQUIRE(QueryUnbiasedInterruptTime(&value) != FALSE);
    return value;
}

[[nodiscard]] MeasuredInterval measure_interval(
    const LARGE_INTEGER frequency,
    const DWORD sleep_milliseconds) {
    const auto reality_start = unbiased_time_100ns();
    const auto start = sample_time();
    Sleep(sleep_milliseconds);
    const auto finish = sample_time();
    const auto reality_finish = unbiased_time_100ns();

    return {
        .reality_milliseconds =
            static_cast<double>(reality_finish - reality_start) / 10'000.0,
        .qpc_milliseconds =
            static_cast<double>(finish.qpc - start.qpc) * 1'000.0 /
            static_cast<double>(frequency.QuadPart),
        .tick64_milliseconds = static_cast<double>(finish.tick64 - start.tick64),
        .tick32_milliseconds =
            static_cast<double>(static_cast<std::uint32_t>(finish.tick32 - start.tick32)),
        .multimedia_milliseconds = static_cast<double>(
            static_cast<std::uint32_t>(finish.multimedia - start.multimedia)),
    };
}

void require_api_consistency(
    const MeasuredInterval& interval,
    const double tolerance_milliseconds = 2.0) {
    const std::array elapsed{
        interval.qpc_milliseconds,
        interval.tick64_milliseconds,
        interval.tick32_milliseconds,
        interval.multimedia_milliseconds,
    };
    const auto [minimum, maximum] = std::minmax_element(elapsed.begin(), elapsed.end());
    REQUIRE(*maximum - *minimum <= tolerance_milliseconds);
}

void require_speed_ratio(
    const MeasuredInterval& interval,
    const double expected,
    const double tolerance) {
    const auto ratio = interval.qpc_milliseconds / interval.reality_milliseconds;
    REQUIRE(std::abs(ratio - expected) <= tolerance);
}

[[nodiscard]] RuntimeStateV1 state(const HookApi& api) {
    RuntimeStateV1 value;
    REQUIRE(api.get_state(&value) == HookApiResult::ok);
    return value;
}

[[nodiscard]] constexpr std::uint64_t file_time_to_uint64(
    const FILETIME value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           value.dwLowDateTime;
}

template <std::size_t Size>
[[nodiscard]] std::array<std::byte, Size> random_bytes() {
    std::array<std::byte, Size> output{};
    REQUIRE(BCryptGenRandom(
                nullptr,
                reinterpret_cast<PUCHAR>(output.data()),
                static_cast<ULONG>(output.size()),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0);
    return output;
}

template <std::size_t Size>
[[nodiscard]] std::wstring to_hex(const std::array<std::byte, Size>& bytes) {
    constexpr std::wstring_view digits{L"0123456789abcdef"};
    std::wstring output;
    output.reserve(Size * 2);
    for (const auto value : bytes) {
        const auto byte = std::to_integer<std::uint8_t>(value);
        output.push_back(digits[byte >> 4U]);
        output.push_back(digits[byte & 0x0FU]);
    }
    return output;
}

[[nodiscard]] std::wstring quote_argument(const std::wstring& argument) {
    REQUIRE(argument.find(L'"') == std::wstring::npos);
    return L'"' + argument + L'"';
}

[[nodiscard]] ControllerProcess start_controller(
    const std::filesystem::path& executable,
    const std::wstring& mode,
    const std::wstring& pipe_name,
    const std::wstring& session_hex,
    const std::wstring& token_hex) {
    auto command_line =
        quote_argument(executable.wstring()) + L' ' + quote_argument(mode) + L' ' +
        quote_argument(pipe_name) + L' ' + quote_argument(session_hex) + L' ' +
        quote_argument(token_hex);
    std::vector<wchar_t> mutable_command(
        command_line.begin(),
        command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process_info{};
    REQUIRE(CreateProcessW(
                executable.c_str(),
                mutable_command.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startup,
                &process_info) != FALSE);

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    REQUIRE(GetProcessTimes(
                process_info.hProcess,
                &creation,
                &exit,
                &kernel,
                &user) != FALSE);

    return {
        .process = UniqueHandle{process_info.hProcess},
        .thread = UniqueHandle{process_info.hThread},
        .process_id = process_info.dwProcessId,
        .creation_time = file_time_to_uint64(creation),
    };
}

void wait_until_pipe_exists(const std::wstring& pipe_name) {
    const auto deadline = unbiased_time_100ns() + 30'000'000ULL;
    while (WaitNamedPipeW(pipe_name.c_str(), 50) == FALSE) {
        const auto error = GetLastError();
        REQUIRE(error == ERROR_FILE_NOT_FOUND || error == ERROR_SEM_TIMEOUT);
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(10);
    }
}

template <typename Predicate>
[[nodiscard]] RuntimeStateV1 wait_for_state(
    const HookApi& api,
    Predicate predicate,
    const std::uint64_t timeout_100ns = 30'000'000ULL) {
    const auto deadline = unbiased_time_100ns() + timeout_100ns;
    for (;;) {
        const auto current = state(api);
        if (predicate(current)) {
            return current;
        }
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(5);
    }
}

[[nodiscard]] HookApi load_hook_api(
    const std::filesystem::path& hook_path) {
    const auto module = LoadLibraryW(hook_path.c_str());
    if (module == nullptr) {
        throw TestFailure(
            "LoadLibraryW failed with error " + std::to_string(GetLastError()));
    }

    return {
        .initialize = resolve<InitializeFunction>(module, "TimeTransleInitialize"),
        .set_speed = resolve<SetSpeedFunction>(module, "TimeTransleSetSpeed"),
        .set_paused = resolve<SetPausedFunction>(module, "TimeTransleSetPaused"),
        .reset = resolve<ResetFunction>(module, "TimeTransleReset"),
        .deactivate = resolve<DeactivateFunction>(module, "TimeTransleDeactivate"),
        .get_state = resolve<GetStateFunction>(module, "TimeTransleGetState"),
    };
}

[[nodiscard]] HookApi resolve_loaded_hook_api(const HMODULE module) {
    return {
        .initialize = resolve<InitializeFunction>(module, "TimeTransleInitialize"),
        .set_speed = resolve<SetSpeedFunction>(module, "TimeTransleSetSpeed"),
        .set_paused = resolve<SetPausedFunction>(module, "TimeTransleSetPaused"),
        .reset = resolve<ResetFunction>(module, "TimeTransleReset"),
        .deactivate = resolve<DeactivateFunction>(module, "TimeTransleDeactivate"),
        .get_state = resolve<GetStateFunction>(module, "TimeTransleGetState"),
    };
}

void run_hook_integration(const std::filesystem::path& hook_path) {
    LARGE_INTEGER frequency_before{};
    REQUIRE(QueryPerformanceFrequency(&frequency_before) != FALSE);

    const auto api = load_hook_api(hook_path);

    REQUIRE(api.set_speed(2.0) == HookApiResult::not_initialized);
    InitializeRequestV1 invalid_request;
    invalid_request.process_id = GetCurrentProcessId() + 1U;
    REQUIRE(api.initialize(&invalid_request) == HookApiResult::invalid_argument);

    InitializeRequestV1 request;
    request.process_id = GetCurrentProcessId();
    REQUIRE(api.initialize(&request) == HookApiResult::ok);
    REQUIRE(api.initialize(&request) == HookApiResult::already_initialized);

    LARGE_INTEGER frequency_after{};
    REQUIRE(QueryPerformanceFrequency(&frequency_after) != FALSE);
    REQUIRE(frequency_after.QuadPart == frequency_before.QuadPart);

    const auto before_transition = sample_time();
    REQUIRE(api.set_speed(2.0) == HookApiResult::ok);
    const auto after_transition = sample_time();
    REQUIRE(after_transition.qpc >= before_transition.qpc);
    REQUIRE(
        static_cast<double>(after_transition.qpc - before_transition.qpc) * 1'000.0 /
            static_cast<double>(frequency_after.QuadPart) <=
        2.0);

    auto interval = measure_interval(frequency_after, 120);
    require_api_consistency(interval);
    require_speed_ratio(interval, 2.0, 0.2);

    REQUIRE(api.set_speed(0.5) == HookApiResult::ok);
    interval = measure_interval(frequency_after, 120);
    require_api_consistency(interval);
    require_speed_ratio(interval, 0.5, 0.12);

    REQUIRE(api.set_paused(1) == HookApiResult::ok);
    interval = measure_interval(frequency_after, 80);
    require_api_consistency(interval);
    REQUIRE(interval.qpc_milliseconds <= 0.1);
    REQUIRE(interval.tick64_milliseconds == 0.0);
    REQUIRE(interval.tick32_milliseconds == 0.0);
    REQUIRE(interval.multimedia_milliseconds == 0.0);

    REQUIRE(api.set_speed(8.0) == HookApiResult::ok);
    REQUIRE(state(api).configured_speed == 8.0);
    REQUIRE(state(api).paused == 1U);

    REQUIRE(api.reset() == HookApiResult::ok);
    const auto reset_state = state(api);
    REQUIRE(reset_state.configured_speed == 1.0);
    REQUIRE(reset_state.paused == 0U);
    interval = measure_interval(frequency_after, 100);
    require_api_consistency(interval);
    require_speed_ratio(interval, 1.0, 0.15);

    REQUIRE(api.deactivate() == HookApiResult::ok);
    const auto inactive_state = state(api);
    REQUIRE(inactive_state.configured_speed == 1.0);
    REQUIRE(inactive_state.paused == 0U);
    REQUIRE(inactive_state.accepting_commands == 0U);
    REQUIRE(api.set_speed(2.0) == HookApiResult::inactive);

    interval = measure_interval(frequency_after, 100);
    require_api_consistency(interval);
    require_speed_ratio(interval, 1.0, 0.15);

    // V1 logical deactivation intentionally leaves the Hook DLL resident.
}

void run_ipc_integration(
    const std::wstring& mode,
    const std::filesystem::path& hook_path,
    const std::filesystem::path& controller_path) {
    const auto session_id = random_bytes<16>();
    const auto token = random_bytes<32>();
    const auto session_hex = to_hex(session_id);
    const auto token_hex = to_hex(token);
    const auto pipe_name =
        LR"(\\.\pipe\TimeTransle.Test.)" + std::to_wstring(GetCurrentProcessId()) +
        L'.' + session_hex;

    auto controller = start_controller(
        controller_path,
        mode,
        pipe_name,
        session_hex,
        token_hex);
    wait_until_pipe_exists(pipe_name);

    LARGE_INTEGER frequency{};
    REQUIRE(QueryPerformanceFrequency(&frequency) != FALSE);
    const auto api = load_hook_api(hook_path);

    InitializeRequestV1 request;
    request.process_id = GetCurrentProcessId();
    request.flags =
        timetransle::hook::product_bootstrap_flag |
        timetransle::hook::enable_ipc_flag;
    request.controller_process_id = controller.process_id;
    request.controller_creation_time = controller.creation_time;
    request.session_id = session_id;
    request.session_token = token;
    REQUIRE(pipe_name.size() + 1 <= request.pipe_name.size());
    std::copy(pipe_name.begin(), pipe_name.end(), request.pipe_name.begin());
    request.pipe_name[pipe_name.size()] = L'\0';
    REQUIRE(api.initialize(&request) == HookApiResult::ok);

    if (mode == L"disconnect") {
        const auto active = wait_for_state(api, [](const RuntimeStateV1& current) {
            return current.ipc_state == IpcRuntimeState::active &&
                   current.configured_speed == 2.0 &&
                   current.last_applied_command_sequence == 5;
        });
        REQUIRE(active.disconnect_recovery_occurred == 0U);
        auto interval = measure_interval(frequency, 100);
        require_api_consistency(interval);
        require_speed_ratio(interval, 2.0, 0.2);
    }

    REQUIRE(WaitForSingleObject(controller.process.get(), 5'000) == WAIT_OBJECT_0);
    DWORD controller_exit_code{};
    REQUIRE(GetExitCodeProcess(
                controller.process.get(),
                &controller_exit_code) != FALSE);
    REQUIRE(controller_exit_code == 0);

    if (mode == L"shutdown") {
        const auto stopped = wait_for_state(api, [](const RuntimeStateV1& current) {
            return current.ipc_state == IpcRuntimeState::shutdown;
        });
        REQUIRE(stopped.configured_speed == 1.0);
        REQUIRE(stopped.paused == 0U);
        REQUIRE(stopped.accepting_commands == 0U);
        REQUIRE(stopped.disconnect_recovery_occurred == 0U);
        REQUIRE(stopped.last_applied_command_sequence == 2);
    } else {
        const auto recovered = wait_for_state(api, [](const RuntimeStateV1& current) {
            return current.ipc_state == IpcRuntimeState::disconnected &&
                   current.disconnect_recovery_occurred != 0U;
        });
        REQUIRE(recovered.configured_speed == 1.0);
        REQUIRE(recovered.paused == 0U);
        REQUIRE(recovered.accepting_commands == 0U);

        const auto interval = measure_interval(frequency, 80);
        require_api_consistency(interval);
        require_speed_ratio(interval, 1.0, 0.18);
    }
}

void wait_for_injection_and_validate(const std::wstring& hook_module_name) {
    const auto deadline = unbiased_time_100ns() + 50'000'000ULL;
    HMODULE module{};
    do {
        module = GetModuleHandleW(hook_module_name.c_str());
        if (module == nullptr) {
            REQUIRE(unbiased_time_100ns() < deadline);
            Sleep(5);
        }
    } while (module == nullptr);

    const auto api = resolve_loaded_hook_api(module);
    RuntimeStateV1 initialized_state;
    for (;;) {
        const auto result = api.get_state(&initialized_state);
        if (result == HookApiResult::ok &&
            initialized_state.configured_speed == 1.0 &&
            initialized_state.paused == 0U &&
            initialized_state.accepting_commands != 0U) {
            break;
        }
        REQUIRE(
            result == HookApiResult::not_initialized ||
            result == HookApiResult::ok);
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(5);
    }
    REQUIRE(initialized_state.ipc_state == IpcRuntimeState::disabled);

    LARGE_INTEGER frequency{};
    REQUIRE(QueryPerformanceFrequency(&frequency) != FALSE);
    REQUIRE(api.set_speed(2.0) == HookApiResult::ok);
    const auto interval = measure_interval(frequency, 100);
    require_api_consistency(interval);
    require_speed_ratio(interval, 2.0, 0.2);

    // Keep the process alive long enough for the harness to verify idempotence.
    Sleep(1'500);
}

void wait_for_controller_e2e(
    const std::wstring& hook_module_name) {
    controller_e2e_failure_code = 10;
    const auto deadline =
        unbiased_time_100ns() + 100'000'000ULL;
    HMODULE module{};
    while (module == nullptr) {
        module = GetModuleHandleW(hook_module_name.c_str());
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(5);
    }
    const auto api = resolve_loaded_hook_api(module);
    RuntimeStateV1 connected;
    for (;;) {
        const auto result = api.get_state(&connected);
        if (result == HookApiResult::ok &&
            connected.ipc_state == IpcRuntimeState::active &&
            connected.accepting_commands != 0U) {
            break;
        }
        REQUIRE(
            result == HookApiResult::not_initialized ||
            result == HookApiResult::ok);
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(5);
    }
    REQUIRE(connected.paused == 0U);

    LARGE_INTEGER frequency{};
    REQUIRE(QueryPerformanceFrequency(&frequency) != FALSE);

    controller_e2e_failure_code = 20;
    const auto fast = wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.configured_speed == 2.0 &&
                   current.paused == 0U &&
                   current.last_applied_command_sequence >= 1;
        });
    static_cast<void>(fast);
    auto interval = measure_interval(frequency, 100);
    require_api_consistency(interval);
    require_speed_ratio(interval, 2.0, 0.2);

    controller_e2e_failure_code = 30;
    const auto paused = wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.paused != 0U &&
                   current.last_applied_command_sequence >= 2;
        });
    static_cast<void>(paused);
    LARGE_INTEGER pause_start{};
    LARGE_INTEGER pause_end{};
    REQUIRE(QueryPerformanceCounter(&pause_start) != FALSE);
    Sleep(100);
    REQUIRE(QueryPerformanceCounter(&pause_end) != FALSE);
    REQUIRE(
        pause_end.QuadPart - pause_start.QuadPart <
        frequency.QuadPart / 100);

    controller_e2e_failure_code = 40;
    const auto resumed = wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.paused == 0U &&
                   current.last_applied_command_sequence >= 3;
        });
    static_cast<void>(resumed);

    controller_e2e_failure_code = 50;
    const auto slow = wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.configured_speed == 0.5 &&
                   current.paused == 0U &&
                   current.last_applied_command_sequence >= 4;
        });
    static_cast<void>(slow);
    interval = measure_interval(frequency, 100);
    require_api_consistency(interval);
    require_speed_ratio(interval, 0.5, 0.12);

    controller_e2e_failure_code = 60;
    const auto reset = wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.configured_speed == 1.0 &&
                   current.paused == 0U &&
                   current.last_applied_command_sequence >= 5;
        });
    static_cast<void>(reset);
    interval = measure_interval(frequency, 100);
    require_api_consistency(interval);
    require_speed_ratio(interval, 1.0, 0.15);

    controller_e2e_failure_code = 70;
    const auto shutdown = wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.ipc_state ==
                       IpcRuntimeState::shutdown &&
                   current.accepting_commands == 0U;
        },
        100'000'000ULL);
    REQUIRE(shutdown.configured_speed == 1.0);
    REQUIRE(shutdown.paused == 0U);
}

void wait_for_controller_disconnect_e2e(
    const std::wstring& hook_module_name) {
    controller_e2e_failure_code = 80;
    const auto deadline =
        unbiased_time_100ns() + 100'000'000ULL;
    HMODULE module{};
    while (module == nullptr) {
        module = GetModuleHandleW(hook_module_name.c_str());
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(5);
    }
    const auto api = resolve_loaded_hook_api(module);
    RuntimeStateV1 connected;
    for (;;) {
        const auto result = api.get_state(&connected);
        if (result == HookApiResult::ok &&
            connected.ipc_state == IpcRuntimeState::active &&
            connected.accepting_commands != 0U) {
            break;
        }
        REQUIRE(
            result == HookApiResult::not_initialized ||
            result == HookApiResult::ok);
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(5);
    }

    controller_e2e_failure_code = 81;
    LARGE_INTEGER frequency{};
    REQUIRE(QueryPerformanceFrequency(&frequency) != FALSE);
    const auto fast = wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.configured_speed == 2.0 &&
                   current.paused == 0U &&
                   current.last_applied_command_sequence >= 1;
        });
    static_cast<void>(fast);
    auto interval = measure_interval(frequency, 100);
    require_api_consistency(interval);
    require_speed_ratio(interval, 2.0, 0.2);

    controller_e2e_failure_code = 82;
    const auto recovered = wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.ipc_state ==
                       IpcRuntimeState::disconnected &&
                   current.configured_speed == 1.0 &&
                   current.paused == 0U &&
                   current.accepting_commands == 0U &&
                   current.disconnect_recovery_occurred != 0U;
        },
        100'000'000ULL);
    static_cast<void>(recovered);
    interval = measure_interval(frequency, 100);
    require_api_consistency(interval);
    require_speed_ratio(interval, 1.0, 0.18);
}

void wait_for_controller_endurance_e2e(
    const std::wstring& hook_module_name) {
    controller_e2e_failure_code = 90;
    const auto deadline =
        unbiased_time_100ns() + 100'000'000ULL;
    HMODULE module{};
    while (module == nullptr) {
        module = GetModuleHandleW(hook_module_name.c_str());
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(20);
    }
    const auto api = resolve_loaded_hook_api(module);
    for (;;) {
        RuntimeStateV1 current;
        const auto result = api.get_state(&current);
        if (result == HookApiResult::ok &&
            current.ipc_state == IpcRuntimeState::active &&
            current.accepting_commands != 0U) {
            break;
        }
        REQUIRE(
            result == HookApiResult::not_initialized ||
            result == HookApiResult::ok);
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(5);
    }

    LARGE_INTEGER frequency{};
    REQUIRE(QueryPerformanceFrequency(&frequency) != FALSE);

    controller_e2e_failure_code = 91;
    static_cast<void>(wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.configured_speed == 2.0 &&
                   current.paused == 0U &&
                   current.last_applied_command_sequence >= 1;
        }));
    auto interval = measure_interval(frequency, 5'000);
    require_api_consistency(interval, 20.0);
    require_speed_ratio(interval, 2.0, 0.02);

    controller_e2e_failure_code = 92;
    static_cast<void>(wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.configured_speed == 0.5 &&
                   current.paused == 0U &&
                   current.last_applied_command_sequence >= 2;
        }));
    interval = measure_interval(frequency, 10'000);
    require_api_consistency(interval, 20.0);
    require_speed_ratio(interval, 0.5, 0.01);

    controller_e2e_failure_code = 93;
    static_cast<void>(wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.ipc_state ==
                       IpcRuntimeState::shutdown &&
                   current.configured_speed == 1.0 &&
                   current.paused == 0U &&
                   current.accepting_commands == 0U;
        },
        100'000'000ULL));
}

void wait_for_controller_lifecycle_e2e(
    const std::wstring& hook_module_name) {
    controller_e2e_failure_code = 100;
    const auto deadline =
        unbiased_time_100ns() + 100'000'000ULL;
    HMODULE module{};
    while (module == nullptr) {
        module = GetModuleHandleW(hook_module_name.c_str());
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(20);
    }
    const auto api = resolve_loaded_hook_api(module);
    for (;;) {
        RuntimeStateV1 current;
        const auto result = api.get_state(&current);
        if (result == HookApiResult::ok &&
            current.ipc_state == IpcRuntimeState::active &&
            current.configured_speed == 1.0 &&
            current.paused == 0U &&
            current.accepting_commands != 0U) {
            break;
        }
        REQUIRE(
            result == HookApiResult::not_initialized ||
            result == HookApiResult::ok);
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(5);
    }
    controller_e2e_failure_code = 101;
    static_cast<void>(wait_for_state(
        api,
        [](const RuntimeStateV1& current) {
            return current.ipc_state ==
                       IpcRuntimeState::shutdown &&
                   current.configured_speed == 1.0 &&
                   current.paused == 0U &&
                   current.accepting_commands == 0U;
        },
        100'000'000ULL));
}

void wait_for_controller_smoke(
    const std::wstring& hook_module_name) {
    const auto deadline =
        unbiased_time_100ns() + 3'000'000'000ULL;
    HMODULE module{};
    while (module == nullptr) {
        module = GetModuleHandleW(hook_module_name.c_str());
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(20);
    }
    const auto api = resolve_loaded_hook_api(module);
    bool was_active = false;
    for (;;) {
        RuntimeStateV1 current;
        const auto result = api.get_state(&current);
        REQUIRE(
            result == HookApiResult::not_initialized ||
            result == HookApiResult::ok);
        if (result == HookApiResult::ok) {
            was_active =
                was_active ||
                current.ipc_state == IpcRuntimeState::active;
            if (was_active &&
                (current.ipc_state ==
                     IpcRuntimeState::shutdown ||
                 current.ipc_state ==
                     IpcRuntimeState::disconnected)) {
                REQUIRE(current.configured_speed == 1.0);
                REQUIRE(current.paused == 0U);
                return;
            }
        }
        REQUIRE(unbiased_time_100ns() < deadline);
        Sleep(20);
    }
}

}  // namespace

int wmain(const int argument_count, wchar_t** const arguments) {
    try {
        if (argument_count == 3 && std::wstring_view{arguments[1]} == L"--direct") {
            run_hook_integration(std::filesystem::path{arguments[2]});
        } else if (
            argument_count == 5 &&
            std::wstring_view{arguments[1]} == L"--ipc") {
            run_ipc_integration(
                arguments[2],
                std::filesystem::path{arguments[3]},
                std::filesystem::path{arguments[4]});
        } else if (
            argument_count == 3 &&
            std::wstring_view{arguments[1]} == L"--wait-injection") {
            wait_for_injection_and_validate(arguments[2]);
        } else if (
            argument_count == 3 &&
            std::wstring_view{arguments[1]} ==
                L"--controller-e2e") {
            wait_for_controller_e2e(arguments[2]);
        } else if (
            argument_count == 3 &&
            std::wstring_view{arguments[1]} ==
                L"--controller-disconnect-e2e") {
            wait_for_controller_disconnect_e2e(arguments[2]);
        } else if (
            argument_count == 3 &&
            std::wstring_view{arguments[1]} ==
                L"--controller-endurance-e2e") {
            wait_for_controller_endurance_e2e(arguments[2]);
        } else if (
            argument_count == 3 &&
            std::wstring_view{arguments[1]} ==
                L"--controller-lifecycle-e2e") {
            wait_for_controller_lifecycle_e2e(arguments[2]);
        } else if (
            argument_count == 3 &&
            std::wstring_view{arguments[1]} ==
                L"--controller-smoke") {
            wait_for_controller_smoke(arguments[2]);
        } else {
            std::cerr
                << "usage: TimeTransleTestApp --direct <hook> | "
                   "--ipc <mode> <hook> <controller-stub> | "
                   "--wait-injection <hook-module-name> | "
                   "--controller-e2e <hook-module-name> | "
                   "--controller-disconnect-e2e "
                   "<hook-module-name> | "
                   "--controller-endurance-e2e "
                   "<hook-module-name> | "
                   "--controller-lifecycle-e2e "
                   "<hook-module-name> | "
                   "--controller-smoke "
                   "<hook-module-name>\n";
            return 2;
        }

        std::cout << "TimeTransle integration passed for "
                  << (sizeof(void*) == 8 ? "x64" : "x86") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TimeTransle Hook integration failed: " << error.what() << '\n';
        return argument_count >= 2 &&
                       (std::wstring_view{arguments[1]} ==
                            L"--controller-e2e" ||
                        std::wstring_view{arguments[1]} ==
                            L"--controller-disconnect-e2e" ||
                        std::wstring_view{arguments[1]} ==
                            L"--controller-endurance-e2e" ||
                        std::wstring_view{arguments[1]} ==
                            L"--controller-lifecycle-e2e")
                   ? controller_e2e_failure_code
                   : 1;
    }
}
