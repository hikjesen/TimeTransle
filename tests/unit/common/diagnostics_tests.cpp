#include <windows.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "timetransle/common/diagnostics.hpp"

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

[[nodiscard]] std::wstring current_local_app_data() {
    std::wstring buffer(32'768, L'\0');
    const auto length = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    REQUIRE(length > 0 && length < buffer.size());
    buffer.resize(length);
    return buffer;
}

class EnvironmentRestore final {
public:
    explicit EnvironmentRestore(std::wstring value)
        : value_{std::move(value)} {}

    ~EnvironmentRestore() {
        SetEnvironmentVariableW(
            L"LOCALAPPDATA",
            value_.c_str());
    }

    EnvironmentRestore(const EnvironmentRestore&) = delete;
    EnvironmentRestore& operator=(
        const EnvironmentRestore&) = delete;

private:
    std::wstring value_;
};

}  // namespace

int main() {
    try {
        const auto original = current_local_app_data();
        EnvironmentRestore restore{original};
        const auto root =
            std::filesystem::temp_directory_path() /
            (L"TimeTransleDiagnosticsTest-" +
             std::to_wstring(GetCurrentProcessId()));
        std::error_code error;
        std::filesystem::remove_all(root, error);
        REQUIRE(SetEnvironmentVariableW(
                    L"LOCALAPPDATA",
                    root.c_str()) != FALSE);

        timetransle::common::initialize_diagnostics();
        timetransle::common::write_diagnostic(
            "controller",
            "targetInspection",
            "ready",
            std::nullopt);
        timetransle::common::write_diagnostic(
            "injector",
            "loadLibraryAndHookInitialize",
            "failed",
            "DLL_LOAD_FAILED");

        const auto active =
            root /
            L"TimeTransle" /
            L"logs" /
            L"timetransle.jsonl";
        {
            std::ifstream stream{active, std::ios::binary};
            REQUIRE(stream.good());
            const std::string contents{
                std::istreambuf_iterator<char>{stream},
                std::istreambuf_iterator<char>{}};
            REQUIRE(contents.find("\"time\":") != std::string::npos);
            REQUIRE(contents.find("\"component\":") !=
                    std::string::npos);
            REQUIRE(contents.find("\"stage\":") !=
                    std::string::npos);
            REQUIRE(contents.find("\"result\":") !=
                    std::string::npos);
            REQUIRE(contents.find("\"errorCode\":") !=
                    std::string::npos);
            REQUIRE(contents.find("targetPid") == std::string::npos);
            REQUIRE(contents.find("moduleName") == std::string::npos);
            REQUIRE(contents.find("commandLine") == std::string::npos);
            REQUIRE(contents.find("sessionToken") == std::string::npos);
            REQUIRE(contents.find("\"path\":") == std::string::npos);
        }
        REQUIRE(std::filesystem::file_size(active) <=
                timetransle::common::
                    maximum_diagnostic_file_bytes);

        {
            std::ofstream full{
                active,
                std::ios::binary | std::ios::trunc};
            REQUIRE(full.good());
            full.seekp(
                static_cast<std::streamoff>(
                    timetransle::common::
                        maximum_diagnostic_file_bytes -
                    1U));
            full.put('\n');
        }
        timetransle::common::write_diagnostic(
            "controller",
            "rotation",
            "ok");
        REQUIRE(std::filesystem::is_regular_file(
            std::filesystem::path{
                active.wstring() + L".1"}));
        REQUIRE(std::filesystem::file_size(active) <
                timetransle::common::
                    maximum_diagnostic_file_bytes);

        std::size_t log_file_count{};
        for (const auto& entry :
             std::filesystem::directory_iterator{
                 active.parent_path()}) {
            if (entry.is_regular_file()) {
                ++log_file_count;
            }
        }
        REQUIRE(log_file_count <=
                timetransle::common::diagnostic_file_count);

        std::filesystem::remove_all(root, error);
        std::cout << "diagnostic log contract passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "diagnostic log contract failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
