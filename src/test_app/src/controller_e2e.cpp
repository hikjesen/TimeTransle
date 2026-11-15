#include <Windows.h>

#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

#include "timetransle/controller/hook_session.hpp"
#include "timetransle/protocol/error_code.hpp"
#include "timetransle/safety/windows_inspector.hpp"

namespace {

using timetransle::controller::HookCommand;
using timetransle::controller::HookCommandType;
using timetransle::controller::HookEvent;
using timetransle::controller::HookEventType;
using timetransle::controller::HookRequestIdentity;
using timetransle::controller::HookSession;
using timetransle::safety::MachineType;
using timetransle::safety::ProcessIdentity;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(
    const bool condition,
    const std::string_view expression,
    const std::source_location location =
        std::source_location::current()) {
    if (!condition) {
        throw TestFailure(
            std::string{location.file_name()} + ':' +
            std::to_string(location.line()) +
            " requirement failed: " +
            std::string{expression});
    }
}

#define REQUIRE(expression) require((expression), #expression)

class TestProcess final {
public:
    TestProcess(HANDLE process, HANDLE thread) noexcept
        : process_{process}, thread_{thread} {}

    ~TestProcess() {
        if (process_ != nullptr) {
            if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process_, 99);
                WaitForSingleObject(process_, 5'000);
            }
            CloseHandle(process_);
        }
        if (thread_ != nullptr) {
            CloseHandle(thread_);
        }
    }

    TestProcess(const TestProcess&) = delete;
    TestProcess& operator=(const TestProcess&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return process_;
    }

private:
    HANDLE process_{};
    HANDLE thread_{};
};

[[nodiscard]] std::wstring quote_argument(
    const std::wstring_view argument) {
    return L"\"" + std::wstring{argument} + L"\"";
}

[[nodiscard]] std::filesystem::path executable_directory() {
    std::wstring buffer(32'768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    REQUIRE(length > 0 && length < buffer.size());
    buffer.resize(length);
    return std::filesystem::path{buffer}.parent_path();
}

[[nodiscard]] constexpr std::uint64_t file_time_to_uint64(
    const FILETIME value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           value.dwLowDateTime;
}

struct StartedProcess {
    TestProcess handles;
    ProcessIdentity identity;
};

[[nodiscard]] StartedProcess start_test_app(
    const std::filesystem::path& executable,
    const std::wstring_view mode,
    const std::wstring_view hook_name,
    const MachineType machine) {
    auto command =
        quote_argument(executable.wstring()) +
        L" " + std::wstring{mode} + L" " +
        quote_argument(hook_name);
    command.push_back(L'\0');
    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    REQUIRE(CreateProcessW(
                executable.c_str(),
                command.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                executable.parent_path().c_str(),
                &startup,
                &process) != FALSE);

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    REQUIRE(GetProcessTimes(
                process.hProcess,
                &creation,
                &exit,
                &kernel,
                &user) != FALSE);
    return {
        .handles = TestProcess{
            process.hProcess,
            process.hThread},
        .identity = ProcessIdentity{
            .process_id = process.dwProcessId,
            .creation_time =
                file_time_to_uint64(creation),
            .machine = machine,
        },
    };
}

class EventQueue final {
public:
    void push(HookEvent event) {
        {
            std::lock_guard lock{mutex_};
            events_.push_back(std::move(event));
        }
        condition_.notify_one();
    }

    [[nodiscard]] HookEvent wait() {
        std::unique_lock lock{mutex_};
        REQUIRE(condition_.wait_for(
                    lock,
                    std::chrono::seconds{20},
                    [this] { return !events_.empty(); }));
        auto event = std::move(events_.front());
        events_.pop_front();
        return event;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<HookEvent> events_;
};

void require_success(
    const HookEvent& event,
    const HookEventType expected) {
    if (event.type == HookEventType::failed ||
        event.type == HookEventType::disconnected) {
        throw TestFailure(
            "Hook session failed while waiting for " +
            std::to_string(static_cast<int>(expected)) +
            " at UI sequence " +
            std::to_string(event.request.ui_sequence) +
            ": " +
            std::string{timetransle::protocol::to_string(
                event.error)});
    }
    REQUIRE(event.type == expected);
}

void submit_and_wait(
    HookSession& session,
    EventQueue& events,
    const HookCommandType type,
    const std::uint64_t ui_sequence,
    const double speed = 1.0) {
    REQUIRE(session.submit(HookCommand{
        .type = type,
        .request = HookRequestIdentity{
            .ui_epoch = "e2e-ui",
            .ui_sequence = ui_sequence,
            .target_generation = "e2e-target",
        },
        .speed = speed,
    }));
    const auto applied = events.wait();
    require_success(
        applied,
        HookEventType::command_applied);
    REQUIRE(applied.command_type == type);
    REQUIRE(applied.state.command_sequence > 0);
    REQUIRE(applied.state.clock_revision > 0);
}

void require_process_running(const HANDLE process) {
    if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
        DWORD exit_code{};
        GetExitCodeProcess(process, &exit_code);
        throw TestFailure(
            "TestApp exited early with code " +
            std::to_string(exit_code));
    }
}

void wait_until_safe(
    const ProcessIdentity& identity) {
    const auto readiness_deadline = GetTickCount64() + 3'000;
    for (;;) {
        const auto inspection =
            timetransle::safety::inspect_target(
                identity.process_id,
                identity.creation_time,
                identity.machine,
                true);
        if (inspection.decision.allowed) {
            Sleep(50);
            const auto stable_inspection =
                timetransle::safety::inspect_target(
                    identity.process_id,
                    identity.creation_time,
                    identity.machine,
                    true);
            REQUIRE(stable_inspection.decision.allowed);
            return;
        }
        REQUIRE(
            inspection.decision.error ==
            timetransle::protocol::ErrorCode::
                access_denied);
        REQUIRE(GetTickCount64() < readiness_deadline);
        Sleep(10);
    }
}

void run_graceful(
    const std::filesystem::path& test_app,
    const MachineType machine,
    const std::wstring_view hook_name) {
    auto process =
        start_test_app(
            test_app,
            L"--controller-e2e",
            hook_name,
            machine);
    wait_until_safe(process.identity);
    EventQueue events;
    HookSession session;
    REQUIRE(session.start(
        process.identity,
        executable_directory(),
        HookRequestIdentity{
            .ui_epoch = "e2e-ui",
            .ui_sequence = 1,
            .target_generation = "e2e-target",
        },
        [&events](HookEvent event) {
            events.push(std::move(event));
        }));
    const auto attached = events.wait();
    require_success(attached, HookEventType::attached);
    REQUIRE(attached.state.configured_speed == 1.0);
    REQUIRE(!attached.state.paused);
    Sleep(100);

    submit_and_wait(
        session,
        events,
        HookCommandType::set_speed,
        2,
        2.0);
    Sleep(250);
    require_process_running(process.handles.get());
    submit_and_wait(
        session,
        events,
        HookCommandType::pause,
        3);
    Sleep(150);
    submit_and_wait(
        session,
        events,
        HookCommandType::resume,
        4);
    Sleep(100);
    submit_and_wait(
        session,
        events,
        HookCommandType::set_speed,
        5,
        0.5);
    Sleep(250);
    submit_and_wait(
        session,
        events,
        HookCommandType::reset,
        6);
    Sleep(250);

    session.stop(true);
    REQUIRE(
        WaitForSingleObject(process.handles.get(), 10'000) ==
        WAIT_OBJECT_0);
    DWORD exit_code{};
    REQUIRE(GetExitCodeProcess(
                process.handles.get(),
                &exit_code) != FALSE);
    REQUIRE(exit_code == 0);
}

void run_disconnect(
    const std::filesystem::path& test_app,
    const MachineType machine,
    const std::wstring_view hook_name) {
    auto process = start_test_app(
        test_app,
        L"--controller-disconnect-e2e",
        hook_name,
        machine);
    wait_until_safe(process.identity);
    EventQueue events;
    HookSession session;
    REQUIRE(session.start(
        process.identity,
        executable_directory(),
        HookRequestIdentity{
            .ui_epoch = "e2e-ui",
            .ui_sequence = 1,
            .target_generation = "e2e-target",
        },
        [&events](HookEvent event) {
            events.push(std::move(event));
        }));
    require_success(
        events.wait(),
        HookEventType::attached);
    Sleep(100);
    submit_and_wait(
        session,
        events,
        HookCommandType::set_speed,
        2,
        2.0);
    Sleep(250);
    require_process_running(process.handles.get());
    session.stop(false);

    REQUIRE(
        WaitForSingleObject(process.handles.get(), 10'000) ==
        WAIT_OBJECT_0);
    DWORD exit_code{};
    REQUIRE(GetExitCodeProcess(
                process.handles.get(),
                &exit_code) != FALSE);
    REQUIRE(exit_code == 0);
}

void run_endurance(
    const std::filesystem::path& test_app,
    const MachineType machine,
    const std::wstring_view hook_name) {
    auto process = start_test_app(
        test_app,
        L"--controller-endurance-e2e",
        hook_name,
        machine);
    wait_until_safe(process.identity);
    EventQueue events;
    HookSession session;
    REQUIRE(session.start(
        process.identity,
        executable_directory(),
        HookRequestIdentity{
            .ui_epoch = "e2e-ui",
            .ui_sequence = 1,
            .target_generation = "e2e-endurance",
        },
        [&events](HookEvent event) {
            events.push(std::move(event));
        }));
    require_success(
        events.wait(),
        HookEventType::attached);

    submit_and_wait(
        session,
        events,
        HookCommandType::set_speed,
        2,
        2.0);
    Sleep(5'250);
    require_process_running(process.handles.get());
    submit_and_wait(
        session,
        events,
        HookCommandType::set_speed,
        3,
        0.5);
    Sleep(10'250);
    require_process_running(process.handles.get());
    session.stop(true);

    REQUIRE(
        WaitForSingleObject(process.handles.get(), 10'000) ==
        WAIT_OBJECT_0);
    DWORD exit_code{};
    REQUIRE(GetExitCodeProcess(
                process.handles.get(),
                &exit_code) != FALSE);
    REQUIRE(exit_code == 0);
}

void run_lifecycle(
    const std::filesystem::path& test_app,
    const MachineType machine,
    const std::wstring_view hook_name) {
    HookSession session;
    for (std::uint64_t cycle = 0; cycle < 100; ++cycle) {
        auto process = start_test_app(
            test_app,
            L"--controller-lifecycle-e2e",
            hook_name,
            machine);
        wait_until_safe(process.identity);
        EventQueue events;
        REQUIRE(session.start(
            process.identity,
            executable_directory(),
            HookRequestIdentity{
                .ui_epoch = "e2e-ui",
                .ui_sequence = cycle + 1,
                .target_generation =
                    "e2e-lifecycle-" +
                    std::to_string(cycle),
            },
            [&events](HookEvent event) {
                events.push(std::move(event));
            }));
        require_success(
            events.wait(),
            HookEventType::attached);
        Sleep(50);
        session.stop(true);

        REQUIRE(
            WaitForSingleObject(
                process.handles.get(),
                10'000) == WAIT_OBJECT_0);
        DWORD exit_code{};
        REQUIRE(GetExitCodeProcess(
                    process.handles.get(),
                    &exit_code) != FALSE);
        if (exit_code != 0) {
            throw TestFailure(
                "Lifecycle cycle " +
                std::to_string(cycle) +
                " exited with code " +
                std::to_string(exit_code));
        }
    }
}

}  // namespace

int wmain(const int argument_count, wchar_t** const arguments) {
    if (argument_count != 4 && argument_count != 5) {
        std::cerr
            << "usage: ControllerE2E <test-app> <x86|x64> "
               "<hook-module-name> [ordinary]\n";
        return 2;
    }
    try {
        const auto architecture =
            std::wstring_view{arguments[2]};
        const auto machine =
            architecture == L"x64"
                ? MachineType::x64
                : architecture == L"x86"
                    ? MachineType::x86
                    : MachineType::unknown;
        REQUIRE(machine != MachineType::unknown);
        const auto ordinary_mode =
            argument_count == 5 &&
            std::wstring_view{arguments[4]} == L"ordinary";
        REQUIRE(argument_count == 4 || ordinary_mode);
        if (ordinary_mode) {
            auto warmup = start_test_app(
                arguments[1],
                L"--controller-e2e",
                arguments[3],
                machine);
            Sleep(1'000);
            require_process_running(warmup.handles.get());
        }
        run_graceful(arguments[1], machine, arguments[3]);
        run_disconnect(arguments[1], machine, arguments[3]);
        if (!ordinary_mode) {
            run_endurance(arguments[1], machine, arguments[3]);
            run_lifecycle(arguments[1], machine, arguments[3]);
        }
        std::cout << "Controller end-to-end passed for "
                  << (machine == MachineType::x64 ? "x64" : "x86")
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Controller end-to-end failed: "
                  << error.what() << '\n';
        return 1;
    }
}
