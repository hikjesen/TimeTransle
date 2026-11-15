#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace timetransle::common {

inline constexpr std::uintmax_t maximum_diagnostic_file_bytes =
    2U * 1024U * 1024U;
inline constexpr unsigned diagnostic_file_count = 5;

[[nodiscard]] std::optional<std::filesystem::path>
diagnostic_log_directory() noexcept;

void initialize_diagnostics() noexcept;

void write_diagnostic(
    std::string_view component,
    std::string_view stage,
    std::string_view result,
    std::optional<std::string_view> error_code = std::nullopt) noexcept;

}  // namespace timetransle::common
