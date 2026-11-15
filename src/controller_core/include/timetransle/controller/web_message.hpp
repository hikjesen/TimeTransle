#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace timetransle::controller {

inline constexpr std::string_view trusted_webview_origin{
    "https://app.timetransle.local/"};
inline constexpr std::string_view trusted_webview_document{
    "https://app.timetransle.local/index.html"};
inline constexpr std::size_t maximum_web_message_bytes = 65'536;

enum class UiCommandType {
    launch_program,
    save_checkpoint,
    load_checkpoint,
    list_processes,
    select_target,
    attach_target,
    set_speed,
    pause,
    resume,
    reset,
    get_status,
    deactivate,
    relaunch_elevated,
    open_diagnostics,
};

struct UiCommand {
    UiCommandType type{};
    std::string ui_epoch;
    std::uint64_t ui_sequence{};
    std::string target_generation;
    std::uint32_t process_id{};
    std::uint64_t creation_time{};
    double speed{1.0};
};

enum class WebMessageError {
    none,
    untrusted_origin,
    invalid_json,
    schema_violation,
};

struct WebMessageResult {
    WebMessageError error{WebMessageError::invalid_json};
    UiCommand command;
};

[[nodiscard]] WebMessageResult validate_web_message(
    std::string_view source,
    std::string_view json_text);

[[nodiscard]] bool is_trusted_webview_source(
    std::string_view source) noexcept;

[[nodiscard]] std::string make_bridge_response(
    const WebMessageResult& result);

}  // namespace timetransle::controller
