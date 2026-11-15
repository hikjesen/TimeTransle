#include <Windows.h>

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "timetransle/protocol/error_code.hpp"

namespace {

using timetransle::protocol::ErrorCode;

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

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

private:
    HANDLE handle_{};
};

struct ChildProcess {
    UniqueHandle process;
    UniqueHandle thread;
    std::uint32_t process_id{};
    std::uint64_t creation_time{};
};

[[nodiscard]] constexpr std::uint64_t file_time_to_uint64(
    const FILETIME value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           value.dwLowDateTime;
}

[[nodiscard]] std::wstring quote(const std::wstring& value) {
    REQUIRE(value.find(L'"') == std::wstring::npos);
    return L'"' + value + L'"';
}

[[nodiscard]] ChildProcess start_process(
    const std::filesystem::path& executable,
    const std::wstring& arguments) {
    auto command_line = quote(executable.wstring()) + L' ' + arguments;
    std::vector<wchar_t> mutable_command{
        command_line.begin(),
        command_line.end(),
    };
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION information{};
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
                &information) != FALSE);

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    REQUIRE(GetProcessTimes(
                information.hProcess,
                &creation,
                &exit,
                &kernel,
                &user) != FALSE);
    return {
        .process = UniqueHandle{information.hProcess},
        .thread = UniqueHandle{information.hThread},
        .process_id = information.dwProcessId,
        .creation_time = file_time_to_uint64(creation),
    };
}

[[nodiscard]] DWORD wait_for_exit(
    const ChildProcess& process,
    const DWORD timeout = 10'000) {
    REQUIRE(WaitForSingleObject(process.process.get(), timeout) == WAIT_OBJECT_0);
    DWORD exit_code{};
    REQUIRE(GetExitCodeProcess(process.process.get(), &exit_code) != FALSE);
    return exit_code;
}

[[nodiscard]] DWORD run_injector(
    const std::filesystem::path& injector,
    const std::uint32_t process_id,
    const std::uint64_t creation_time) {
    wchar_t creation_hex[32]{};
    REQUIRE(_ui64tow_s(
                creation_time,
                creation_hex,
                std::size(creation_hex),
                16) == 0);
    auto process = start_process(
        injector,
        std::to_wstring(process_id) + L' ' + creation_hex + L" --self-test");
    return wait_for_exit(process);
}

[[nodiscard]] std::uint64_t current_process_creation_time() {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    REQUIRE(GetProcessTimes(
                GetCurrentProcess(),
                &creation,
                &exit,
                &kernel,
                &user) != FALSE);
    return file_time_to_uint64(creation);
}

void run_injector_integration(
    const std::filesystem::path& injector,
    const std::filesystem::path& test_app,
    const std::wstring& hook_module_name) {
    auto target = start_process(
        test_app,
        L"--wait-injection " + quote(hook_module_name));

    const auto require_injector_result = [](
                                             const DWORD actual,
                                             const ErrorCode expected) {
        if (actual != static_cast<DWORD>(expected)) {
            throw TestFailure(
                "Injector returned " + std::to_string(actual) +
                ", expected " +
                std::to_string(static_cast<std::uint32_t>(expected)));
        }
    };

    require_injector_result(
        run_injector(
            injector,
            target.process_id,
            target.creation_time + 1U),
        ErrorCode::target_identity_changed);
    require_injector_result(
        run_injector(
            injector,
            target.process_id,
            target.creation_time),
        ErrorCode::ok);
    require_injector_result(
        run_injector(
            injector,
            target.process_id,
            target.creation_time),
        ErrorCode::already_attached);

    REQUIRE(wait_for_exit(target) == 0);

    require_injector_result(
        run_injector(
            injector,
            GetCurrentProcessId(),
            current_process_creation_time()),
        ErrorCode::ok);
}

}  // namespace

int wmain(const int argument_count, wchar_t** const arguments) {
    if (argument_count != 4) {
        std::cerr << "usage: InjectorHarness <injector> <test-app> <hook-name>\n";
        return 2;
    }

    try {
        run_injector_integration(
            arguments[1],
            arguments[2],
            arguments[3]);
        std::cout << "TimeTransle Injector integration passed for "
                  << (sizeof(void*) == 8 ? "x64" : "x86") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Injector integration failed: " << error.what() << '\n';
        return 1;
    }
}
