#include "timetransle/common/diagnostics.hpp"

#include <windows.h>

#include <array>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

namespace timetransle::common {
namespace {

constexpr wchar_t log_file_name[] = L"timetransle.jsonl";
constexpr wchar_t diagnostic_mutex_name[] =
    L"Local\\TimeTransle.Diagnostics.V1";

std::mutex process_mutex;

class NamedMutexLock final {
public:
    NamedMutexLock() noexcept
        : mutex_{CreateMutexW(
              nullptr,
              FALSE,
              diagnostic_mutex_name)} {
        if (mutex_ != nullptr) {
            const auto wait = WaitForSingleObject(mutex_, 2'000);
            locked_ =
                wait == WAIT_OBJECT_0 ||
                wait == WAIT_ABANDONED;
        }
    }

    ~NamedMutexLock() {
        if (locked_) {
            ReleaseMutex(mutex_);
        }
        if (mutex_ != nullptr) {
            CloseHandle(mutex_);
        }
    }

    NamedMutexLock(const NamedMutexLock&) = delete;
    NamedMutexLock& operator=(const NamedMutexLock&) = delete;

    [[nodiscard]] bool locked() const noexcept {
        return locked_;
    }

private:
    HANDLE mutex_{};
    bool locked_{};
};

[[nodiscard]] std::filesystem::path rotated_path(
    const std::filesystem::path& active,
    const unsigned index) {
    return std::filesystem::path{
        active.wstring() + L"." + std::to_wstring(index)};
}

void rotate_logs_if_needed(
    const std::filesystem::path& active,
    const std::uintmax_t incoming_bytes) noexcept {
    std::error_code error;
    const auto current_size =
        std::filesystem::is_regular_file(active, error) && !error
            ? std::filesystem::file_size(active, error)
            : 0;
    if (error ||
        current_size + incoming_bytes <=
            maximum_diagnostic_file_bytes) {
        return;
    }

    const auto oldest =
        rotated_path(active, diagnostic_file_count - 1);
    std::filesystem::remove(oldest, error);
    error.clear();
    for (unsigned index = diagnostic_file_count - 1;
         index > 1;
         --index) {
        const auto source = rotated_path(active, index - 1);
        const auto destination = rotated_path(active, index);
        if (!std::filesystem::is_regular_file(source, error) || error) {
            error.clear();
            continue;
        }
        std::filesystem::rename(source, destination, error);
        error.clear();
    }
    if (std::filesystem::is_regular_file(active, error) && !error) {
        std::filesystem::rename(
            active,
            rotated_path(active, 1),
            error);
    }
}

[[nodiscard]] std::string json_escape(
    const std::string_view value) {
    constexpr char digits[]{"0123456789abcdef"};
    std::string output;
    output.reserve(value.size());
    for (const auto character : value) {
        const auto byte =
            static_cast<unsigned char>(character);
        switch (character) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (byte < 0x20U) {
                    output += "\\u00";
                    output.push_back(digits[byte >> 4U]);
                    output.push_back(digits[byte & 0x0FU]);
                } else {
                    output.push_back(character);
                }
                break;
        }
    }
    return output;
}

[[nodiscard]] std::string utc_timestamp() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::array<char, 32> buffer{};
    const auto length = sprintf_s(
        buffer.data(),
        buffer.size(),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds);
    return length > 0
               ? std::string{
                     buffer.data(),
                     static_cast<std::size_t>(length)}
               : std::string{"1970-01-01T00:00:00.000Z"};
}

}  // namespace

std::optional<std::filesystem::path>
diagnostic_log_directory() noexcept {
    try {
        std::wstring buffer(32'768, L'\0');
        const auto length = GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return std::nullopt;
        }
        buffer.resize(length);
        return std::filesystem::path{buffer} /
               L"TimeTransle" /
               L"logs";
    } catch (...) {
        return std::nullopt;
    }
}

void initialize_diagnostics() noexcept {
    try {
        const auto directory = diagnostic_log_directory();
        if (!directory.has_value()) {
            return;
        }
        std::error_code error;
        std::filesystem::create_directories(*directory, error);
        if (error) {
            return;
        }
        std::scoped_lock lock{process_mutex};
        NamedMutexLock named_lock;
        if (!named_lock.locked()) {
            return;
        }
        rotate_logs_if_needed(
            *directory / log_file_name,
            0);
    } catch (...) {
    }
}

void write_diagnostic(
    const std::string_view component,
    const std::string_view stage,
    const std::string_view result,
    const std::optional<std::string_view> error_code) noexcept {
    try {
        constexpr std::size_t maximum_field_bytes = 128;
        if (component.empty() ||
            component.size() > maximum_field_bytes ||
            stage.empty() ||
            stage.size() > maximum_field_bytes ||
            result.empty() ||
            result.size() > maximum_field_bytes ||
            (error_code.has_value() &&
             error_code->size() > maximum_field_bytes)) {
            return;
        }
        const auto directory = diagnostic_log_directory();
        if (!directory.has_value()) {
            return;
        }
        std::error_code error;
        std::filesystem::create_directories(*directory, error);
        if (error) {
            return;
        }

        std::string line =
            "{\"time\":\"" + utc_timestamp() +
            "\",\"component\":\"" + json_escape(component) +
            "\",\"stage\":\"" + json_escape(stage) +
            "\",\"result\":\"" + json_escape(result) +
            "\",\"errorCode\":";
        if (error_code.has_value()) {
            line += "\"" + json_escape(*error_code) + "\"";
        } else {
            line += "null";
        }
        line += "}\n";

        std::scoped_lock lock{process_mutex};
        NamedMutexLock named_lock;
        if (!named_lock.locked()) {
            return;
        }
        const auto active = *directory / log_file_name;
        rotate_logs_if_needed(active, line.size());
        std::ofstream stream{
            active,
            std::ios::binary | std::ios::app};
        if (stream) {
            stream.write(
                line.data(),
                static_cast<std::streamsize>(line.size()));
        }
    } catch (...) {
    }
}

}  // namespace timetransle::common
