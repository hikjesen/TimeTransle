#include <windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "timetransle/protocol/error_code.hpp"

namespace {

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

class UniqueHandle final {
public:
    explicit UniqueHandle(const HANDLE handle = nullptr) noexcept
        : handle_{handle} {}

    ~UniqueHandle() {
        if (handle_ != nullptr &&
            handle_ != INVALID_HANDLE_VALUE) {
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

[[nodiscard]] std::wstring quote_argument(
    const std::wstring_view argument) {
    return L"\"" + std::wstring{argument} + L"\"";
}

[[nodiscard]] constexpr std::uint64_t file_time_to_uint64(
    const FILETIME value) noexcept {
    return (static_cast<std::uint64_t>(
                value.dwHighDateTime)
            << 32U) |
           value.dwLowDateTime;
}

[[nodiscard]] std::wstring lowercase(std::wstring value) {
    std::ranges::transform(
        value,
        value.begin(),
        [](const wchar_t character) {
            return static_cast<wchar_t>(
                std::towlower(character));
        });
    return value;
}

[[nodiscard]] bool module_is_loaded(
    const DWORD process_id,
    const std::wstring_view expected_name) {
    UniqueHandle snapshot{CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        process_id)};
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        return false;
    }
    MODULEENTRY32W module{
        .dwSize = sizeof(MODULEENTRY32W),
    };
    if (Module32FirstW(snapshot.get(), &module) == FALSE) {
        return false;
    }
    const auto expected =
        lowercase(std::wstring{expected_name});
    do {
        if (lowercase(module.szModule) == expected) {
            return true;
        }
    } while (Module32NextW(snapshot.get(), &module) != FALSE);
    return false;
}

[[nodiscard]] DWORD launch_and_wait(
    const std::filesystem::path& executable,
    std::wstring arguments,
    const DWORD timeout) {
    auto command =
        quote_argument(executable.wstring()) + L" " +
        std::move(arguments);
    command.push_back(L'\0');
    STARTUPINFOW startup{
        .cb = sizeof(STARTUPINFOW),
    };
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
    UniqueHandle process_handle{process.hProcess};
    UniqueHandle thread_handle{process.hThread};
    static_cast<void>(thread_handle);
    REQUIRE(WaitForSingleObject(
                process_handle.get(),
                timeout) == WAIT_OBJECT_0);
    DWORD exit_code{};
    REQUIRE(GetExitCodeProcess(
                process_handle.get(),
                &exit_code) != FALSE);
    return exit_code;
}

}  // namespace

int wmain(const int argument_count, wchar_t** const arguments) {
    if (argument_count != 4) {
        std::cerr
            << "usage: SafetyPreflightTests <injector> "
               "<fixture-exe> <AntiCheatFixture.dll>\n";
        return 2;
    }

    try {
        const std::filesystem::path injector{arguments[1]};
        const std::filesystem::path fixture_exe{arguments[2]};
        const std::filesystem::path fixture_dll{arguments[3]};
        auto fixture_command =
            quote_argument(fixture_exe.wstring()) + L" " +
            quote_argument(fixture_dll.wstring()) +
            L" 10000";
        fixture_command.push_back(L'\0');
        STARTUPINFOW startup{
            .cb = sizeof(STARTUPINFOW),
        };
        PROCESS_INFORMATION fixture{};
        REQUIRE(CreateProcessW(
                    fixture_exe.c_str(),
                    fixture_command.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    fixture_exe.parent_path().c_str(),
                    &startup,
                    &fixture) != FALSE);
        UniqueHandle fixture_process{fixture.hProcess};
        UniqueHandle fixture_thread{fixture.hThread};
        static_cast<void>(fixture_thread);

        FILETIME creation{};
        FILETIME exit{};
        FILETIME kernel{};
        FILETIME user{};
        REQUIRE(GetProcessTimes(
                    fixture_process.get(),
                    &creation,
                    &exit,
                    &kernel,
                    &user) != FALSE);

        const auto ready_deadline = GetTickCount64() + 3'000;
        PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamic_code{};
        PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY signature{};
        PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY control_flow{};
        for (;;) {
            const auto policies_ready =
                GetProcessMitigationPolicy(
                    fixture_process.get(),
                    ProcessDynamicCodePolicy,
                    &dynamic_code,
                    sizeof(dynamic_code)) != FALSE &&
                dynamic_code.ProhibitDynamicCode != 0 &&
                GetProcessMitigationPolicy(
                    fixture_process.get(),
                    ProcessSignaturePolicy,
                    &signature,
                    sizeof(signature)) != FALSE &&
                signature.MicrosoftSignedOnly != 0 &&
                GetProcessMitigationPolicy(
                    fixture_process.get(),
                    ProcessControlFlowGuardPolicy,
                    &control_flow,
                    sizeof(control_flow)) != FALSE &&
                control_flow.EnableControlFlowGuard != 0;
            if (policies_ready &&
                module_is_loaded(
                    fixture.dwProcessId,
                    fixture_dll.filename().wstring())) {
                break;
            }
            REQUIRE(
                WaitForSingleObject(
                    fixture_process.get(),
                    0) == WAIT_TIMEOUT);
            REQUIRE(GetTickCount64() < ready_deadline);
            Sleep(10);
        }

        std::array<wchar_t, 32> creation_text{};
        REQUIRE(swprintf_s(
                    creation_text.data(),
                    creation_text.size(),
                    L"%llx",
                    static_cast<unsigned long long>(
                        file_time_to_uint64(creation))) > 0);
        const auto exit_code = launch_and_wait(
            injector,
            std::to_wstring(fixture.dwProcessId) + L" " +
                creation_text.data() + L" --inspect-only",
            10'000);
        REQUIRE(
            exit_code ==
            static_cast<DWORD>(
                timetransle::protocol::ErrorCode::ok));

        TerminateProcess(fixture_process.get(), 0);
        WaitForSingleObject(fixture_process.get(), 5'000);
        std::cout
            << "preflight ignored module names and mitigation policy\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "safety preflight fixture failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
