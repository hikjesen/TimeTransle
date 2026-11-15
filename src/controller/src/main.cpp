#include "timetransle/controller/application.hpp"

#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include <shellapi.h>
#include <windows.h>

#include "timetransle/protocol/error_code.hpp"

namespace {

[[nodiscard]] bool parse_decimal_u64(
    const std::wstring_view text,
    std::uint64_t& output) noexcept {
    if (text.empty() || text.size() > 20) {
        return false;
    }
    std::uint64_t value{};
    for (const auto character : text) {
        if (character < L'0' || character > L'9') {
            return false;
        }
        const auto digit =
            static_cast<std::uint64_t>(character - L'0');
        if (value >
            (std::numeric_limits<std::uint64_t>::max() - digit) /
                10) {
            return false;
        }
        value = value * 10 + digit;
    }
    output = value;
    return true;
}

}  // namespace

int APIENTRY wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show_command) {
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argument_count{};
    auto** const arguments =
        CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        return static_cast<int>(
            timetransle::protocol::ErrorCode::invalid_message);
    }

    if (argument_count == 2 &&
        std::wstring_view{arguments[1]} == L"--self-test") {
        LocalFree(arguments);
        return timetransle::controller::run_controller_self_test();
    }

    timetransle::controller::ControllerLaunchOptions options;
    if (argument_count == 4 &&
        std::wstring_view{arguments[1]} ==
            L"--elevated-target") {
        std::uint64_t process_id{};
        std::uint64_t creation_time{};
        if (!parse_decimal_u64(arguments[2], process_id) ||
            process_id == 0 ||
            process_id >
                std::numeric_limits<std::uint32_t>::max() ||
            !parse_decimal_u64(arguments[3], creation_time) ||
            creation_time == 0) {
            LocalFree(arguments);
            return static_cast<int>(
                timetransle::protocol::ErrorCode::invalid_message);
        }
        options.elevated_target =
            timetransle::safety::ProcessIdentity{
                .process_id =
                    static_cast<std::uint32_t>(process_id),
                .creation_time = creation_time,
                .machine =
                    timetransle::safety::MachineType::unknown,
            };
    } else if (argument_count != 1) {
        LocalFree(arguments);
        return static_cast<int>(
            timetransle::protocol::ErrorCode::invalid_message);
    }
    LocalFree(arguments);
    return timetransle::controller::run_controller(
        instance,
        show_command,
        std::move(options));
}
