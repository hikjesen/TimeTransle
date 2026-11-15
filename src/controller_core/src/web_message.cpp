#include "timetransle/controller/web_message.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

namespace timetransle::controller {
namespace {

using Json = nlohmann::json;

[[nodiscard]] bool identifier_is_valid(const std::string& value) noexcept {
    return !value.empty() && value.size() <= 64 &&
           std::all_of(
               value.begin(),
               value.end(),
               [](const unsigned char character) {
                   return (character >= 'A' && character <= 'Z') ||
                          (character >= 'a' && character <= 'z') ||
                          (character >= '0' && character <= '9') ||
                          character == '-';
               });
}

[[nodiscard]] bool has_exact_keys(
    const Json& value,
    const std::initializer_list<std::string_view> keys) {
    if (!value.is_object() || value.size() != keys.size()) {
        return false;
    }
    return std::all_of(keys.begin(), keys.end(), [&value](const auto key) {
        return value.contains(key);
    });
}

[[nodiscard]] std::optional<UiCommandType> parse_type(
    const std::string_view type) noexcept {
    if (type == "launchProgram") return UiCommandType::launch_program;
    if (type == "saveCheckpoint") return UiCommandType::save_checkpoint;
    if (type == "loadCheckpoint") return UiCommandType::load_checkpoint;
    if (type == "listProcesses") {
        return UiCommandType::list_processes;
    }
    if (type == "selectTarget") {
        return UiCommandType::select_target;
    }
    if (type == "attachTarget") {
        return UiCommandType::attach_target;
    }
    if (type == "setSpeed") {
        return UiCommandType::set_speed;
    }
    if (type == "pause") {
        return UiCommandType::pause;
    }
    if (type == "resume") {
        return UiCommandType::resume;
    }
    if (type == "reset") {
        return UiCommandType::reset;
    }
    if (type == "getStatus") {
        return UiCommandType::get_status;
    }
    if (type == "deactivate") {
        return UiCommandType::deactivate;
    }
    if (type == "relaunchElevated") {
        return UiCommandType::relaunch_elevated;
    }
    if (type == "openDiagnostics") {
        return UiCommandType::open_diagnostics;
    }
    return std::nullopt;
}

[[nodiscard]] bool parse_creation_time(
    const std::string& text,
    std::uint64_t& output) noexcept {
    if (text.empty() || text.size() > 20 ||
        !std::all_of(text.begin(), text.end(), [](const char value) {
            return value >= '0' && value <= '9';
        })) {
        return false;
    }

    const auto result = std::from_chars(
        text.data(),
        text.data() + text.size(),
        output);
    return result.ec == std::errc{} &&
           result.ptr == text.data() + text.size();
}

[[nodiscard]] std::optional<std::uint64_t> parse_nonnegative_integer(
    const Json& value) noexcept {
    try {
        if (value.is_number_unsigned()) {
            return value.get<std::uint64_t>();
        }
        if (value.is_number_integer()) {
            const auto signed_value = value.get<std::int64_t>();
            if (signed_value >= 0) {
                return static_cast<std::uint64_t>(signed_value);
            }
        }
    } catch (const Json::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool parse_common(const Json& json, UiCommand& command) {
    constexpr std::uint64_t maximum_safe_integer = 9'007'199'254'740'991ULL;
    if (!has_exact_keys(
            json,
            {"v", "type", "uiEpoch", "uiSequence", "targetGeneration", "payload"}) ||
        !json["v"].is_number_integer() ||
        json["v"].get<int>() != 1 ||
        !json["type"].is_string() ||
        !json["uiEpoch"].is_string() ||
        !json["targetGeneration"].is_string() ||
        !json["payload"].is_object() ||
        !(json["uiSequence"].is_number_unsigned() ||
          json["uiSequence"].is_number_integer())) {
        return false;
    }

    const auto raw_sequence = parse_nonnegative_integer(json["uiSequence"]);
    if (!raw_sequence.has_value() ||
        *raw_sequence < 1 ||
        *raw_sequence > maximum_safe_integer) {
        return false;
    }

    const auto type = parse_type(json["type"].get_ref<const std::string&>());
    if (!type.has_value()) {
        return false;
    }

    command.type = *type;
    command.ui_epoch = json["uiEpoch"].get<std::string>();
    command.ui_sequence = *raw_sequence;
    command.target_generation = json["targetGeneration"].get<std::string>();
    return identifier_is_valid(command.ui_epoch) &&
           identifier_is_valid(command.target_generation);
}

[[nodiscard]] bool parse_payload(const Json& payload, UiCommand& command) {
    if (command.type == UiCommandType::select_target) {
        if (!has_exact_keys(payload, {"processId", "creationTime"}) ||
            !(payload["processId"].is_number_unsigned() ||
              payload["processId"].is_number_integer()) ||
            !payload["creationTime"].is_string()) {
            return false;
        }
        const auto raw_process_id =
            parse_nonnegative_integer(payload["processId"]);
        if (!raw_process_id.has_value() ||
            *raw_process_id < 1 ||
            *raw_process_id > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        command.process_id = static_cast<std::uint32_t>(*raw_process_id);
        return parse_creation_time(
            payload["creationTime"].get_ref<const std::string&>(),
            command.creation_time);
    }

    if (command.type == UiCommandType::set_speed) {
        if (!has_exact_keys(payload, {"speed"}) ||
            !payload["speed"].is_number()) {
            return false;
        }
        command.speed = payload["speed"].get<double>();
        return std::isfinite(command.speed) &&
               command.speed >= 0.05 &&
               command.speed <= 8.0;
    }

    return payload.empty();
}

[[nodiscard]] std::string_view error_name(const WebMessageError error) noexcept {
    switch (error) {
        case WebMessageError::none:
            return "none";
        case WebMessageError::untrusted_origin:
            return "untrustedOrigin";
        case WebMessageError::invalid_json:
            return "invalidJson";
        case WebMessageError::schema_violation:
            return "schemaViolation";
    }
    return "schemaViolation";
}

}  // namespace

WebMessageResult validate_web_message(
    const std::string_view source,
    const std::string_view json_text) {
    WebMessageResult result;
    if (!is_trusted_webview_source(source)) {
        result.error = WebMessageError::untrusted_origin;
        return result;
    }
    if (json_text.size() > maximum_web_message_bytes) {
        result.error = WebMessageError::schema_violation;
        return result;
    }

    try {
        const auto json = Json::parse(
            json_text.begin(),
            json_text.end(),
            nullptr,
            false,
            true);
        if (json.is_discarded()) {
            result.error = WebMessageError::invalid_json;
            return result;
        }
        if (!parse_common(json, result.command) ||
            !parse_payload(json["payload"], result.command)) {
            result.error = WebMessageError::schema_violation;
            return result;
        }
    } catch (const Json::exception&) {
        result.error = WebMessageError::schema_violation;
        return result;
    }

    result.error = WebMessageError::none;
    return result;
}

bool is_trusted_webview_source(const std::string_view source) noexcept {
    return source == trusted_webview_origin ||
           source == trusted_webview_document;
}

std::string make_bridge_response(const WebMessageResult& result) {
    Json response{
        {"v", 1},
        {"type", result.error == WebMessageError::none
                     ? "commandValidated"
                     : "commandRejected"},
        {"error", error_name(result.error)},
    };
    if (result.error == WebMessageError::none) {
        response["uiEpoch"] = result.command.ui_epoch;
        response["uiSequence"] = result.command.ui_sequence;
        response["targetGeneration"] = result.command.target_generation;
    }
    return response.dump();
}

}  // namespace timetransle::controller
