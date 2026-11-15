#include <array>
#include <exception>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

#include "timetransle/controller/controller_session.hpp"
#include "timetransle/controller/web_message.hpp"

namespace {

using timetransle::controller::CommandAcceptance;
using timetransle::controller::ControllerSession;
using timetransle::controller::SessionStatus;
using timetransle::controller::UiCommand;
using timetransle::controller::UiCommandType;
using timetransle::controller::WebMessageError;

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

constexpr std::string_view valid_set_speed =
    R"({"v":1,"type":"setSpeed","uiEpoch":"ui-1","uiSequence":42,"targetGeneration":"target-1","payload":{"speed":0.5}})";

void web_messages_require_trusted_origin_and_exact_schema() {
    auto result = timetransle::controller::validate_web_message(
        timetransle::controller::trusted_webview_origin,
        valid_set_speed);
    REQUIRE(result.error == WebMessageError::none);
    REQUIRE(result.command.type == UiCommandType::set_speed);
    REQUIRE(result.command.speed == 0.5);

    result = timetransle::controller::validate_web_message(
        "https://evil.invalid/",
        valid_set_speed);
    REQUIRE(result.error == WebMessageError::untrusted_origin);

    result = timetransle::controller::validate_web_message(
        timetransle::controller::trusted_webview_origin,
        R"({"v":1})");
    REQUIRE(result.error == WebMessageError::schema_violation);

    result = timetransle::controller::validate_web_message(
        timetransle::controller::trusted_webview_origin,
        R"({"v":1,"type":"setSpeed","uiEpoch":"ui-1","uiSequence":1,"targetGeneration":"target-1","payload":{"speed":null},"dllPath":"C:\\bad.dll"})");
    REQUIRE(result.error == WebMessageError::schema_violation);

    result = timetransle::controller::validate_web_message(
        timetransle::controller::trusted_webview_document,
        valid_set_speed);
    REQUIRE(result.error == WebMessageError::none);

    result = timetransle::controller::validate_web_message(
        timetransle::controller::trusted_webview_origin,
        R"({"v":1,"type":"setSpeed","uiEpoch":"ui-1","uiSequence":18446744073709551615,"targetGeneration":"target-1","payload":{"speed":0.5}})");
    REQUIRE(result.error == WebMessageError::schema_violation);

    result = timetransle::controller::validate_web_message(
        timetransle::controller::trusted_webview_origin,
        R"({"v":1,"type":"selectTarget","uiEpoch":"ui-1","uiSequence":1,"targetGeneration":"target-1","payload":{"processId":18446744073709551615,"creationTime":"456"}})");
    REQUIRE(result.error == WebMessageError::schema_violation);

    const std::string oversized(
        timetransle::controller::maximum_web_message_bytes + 1,
        ' ');
    result = timetransle::controller::validate_web_message(
        timetransle::controller::trusted_webview_origin,
        oversized);
    REQUIRE(result.error == WebMessageError::schema_violation);
}

void all_command_payload_variants_are_strict() {
    constexpr std::array valid_messages{
        R"({"v":1,"type":"listProcesses","uiEpoch":"ui-1","uiSequence":1,"targetGeneration":"target-1","payload":{}})",
        R"({"v":1,"type":"selectTarget","uiEpoch":"ui-1","uiSequence":2,"targetGeneration":"target-1","payload":{"processId":123,"creationTime":"456"}})",
        R"({"v":1,"type":"attachTarget","uiEpoch":"ui-1","uiSequence":3,"targetGeneration":"target-1","payload":{}})",
        R"({"v":1,"type":"pause","uiEpoch":"ui-1","uiSequence":4,"targetGeneration":"target-1","payload":{}})",
        R"({"v":1,"type":"resume","uiEpoch":"ui-1","uiSequence":5,"targetGeneration":"target-1","payload":{}})",
        R"({"v":1,"type":"reset","uiEpoch":"ui-1","uiSequence":6,"targetGeneration":"target-1","payload":{}})",
        R"({"v":1,"type":"getStatus","uiEpoch":"ui-1","uiSequence":7,"targetGeneration":"target-1","payload":{}})",
        R"({"v":1,"type":"deactivate","uiEpoch":"ui-1","uiSequence":8,"targetGeneration":"target-1","payload":{}})",
        R"({"v":1,"type":"relaunchElevated","uiEpoch":"ui-1","uiSequence":9,"targetGeneration":"target-1","payload":{}})",
        R"({"v":1,"type":"openDiagnostics","uiEpoch":"ui-1","uiSequence":10,"targetGeneration":"target-1","payload":{}})",
    };
    for (const auto message : valid_messages) {
        REQUIRE(timetransle::controller::validate_web_message(
                    timetransle::controller::trusted_webview_origin,
                    message)
                    .error == WebMessageError::none);
    }
}

void controller_session_rejects_stale_ui_and_hook_state() {
    ControllerSession session;
    REQUIRE(!session.accept_hook_ack(
        "target-1",
        0,
        1,
        1.0,
        false,
        timetransle::protocol::TargetRuntime::generic,
        timetransle::protocol::TimeBackend::win32_clock,
        timetransle::protocol::EffectState::unverified));

    const UiCommand select{
        .type = UiCommandType::select_target,
        .ui_epoch = "ui-1",
        .ui_sequence = 1,
        .target_generation = "target-1",
        .process_id = 123,
        .creation_time = 456,
    };
    REQUIRE(session.accept(select) == CommandAcceptance::accepted);
    REQUIRE(session.accept(select) == CommandAcceptance::stale_ui_sequence);
    REQUIRE(session.snapshot().status == SessionStatus::target_selected);

    UiCommand attach{
        .type = UiCommandType::attach_target,
        .ui_epoch = "ui-1",
        .ui_sequence = 2,
        .target_generation = "target-1",
    };
    REQUIRE(session.accept(attach) ==
            CommandAcceptance::invalid_state);
    session.mark_target_checking();
    REQUIRE(session.snapshot().status ==
            SessionStatus::target_checking);
    session.mark_target_ready();
    REQUIRE(session.accept(attach) == CommandAcceptance::accepted);

    session.mark_attaching();
    session.mark_hook_syncing();
    REQUIRE(session.accept_hook_ack(
        "target-1",
        0,
        1,
        1.0,
        false,
        timetransle::protocol::TargetRuntime::unity_mono,
        timetransle::protocol::TimeBackend::unity_mono,
        timetransle::protocol::EffectState::applied));
    REQUIRE(session.snapshot().status == SessionStatus::hooked_synced);

    REQUIRE(session.accept_hook_ack(
        "target-1",
        1,
        2,
        2.0,
        false,
        timetransle::protocol::TargetRuntime::unity_mono,
        timetransle::protocol::TimeBackend::unity_mono,
        timetransle::protocol::EffectState::pending));
    REQUIRE(session.snapshot().applied->effect_state ==
            timetransle::protocol::EffectState::pending);
    REQUIRE(session.accept_hook_ack(
        "target-1",
        1,
        2,
        2.0,
        false,
        timetransle::protocol::TargetRuntime::unity_mono,
        timetransle::protocol::TimeBackend::unity_mono,
        timetransle::protocol::EffectState::applied));
    REQUIRE(session.snapshot().applied->effect_state ==
            timetransle::protocol::EffectState::applied);

    REQUIRE(!session.accept_hook_ack(
        "old-target",
        2,
        3,
        2.0,
        false,
        timetransle::protocol::TargetRuntime::generic,
        timetransle::protocol::TimeBackend::win32_clock,
        timetransle::protocol::EffectState::unverified));
    REQUIRE(!session.accept_hook_ack(
        "target-1",
        0,
        0,
        0.5,
        false,
        timetransle::protocol::TargetRuntime::generic,
        timetransle::protocol::TimeBackend::win32_clock,
        timetransle::protocol::EffectState::unverified));

    UiCommand speed{
        .type = UiCommandType::set_speed,
        .ui_epoch = "ui-1",
        .ui_sequence = 3,
        .target_generation = "target-1",
        .speed = 2.0,
    };
    REQUIRE(session.accept(speed) == CommandAcceptance::accepted);
    speed.target_generation = "old-target";
    speed.ui_sequence = 4;
    REQUIRE(session.accept(speed) ==
            CommandAcceptance::wrong_target_generation);

    session.mark_hook_unknown();
    speed.target_generation = "target-1";
    REQUIRE(session.accept(speed) == CommandAcceptance::invalid_state);
    REQUIRE(!session.snapshot().applied.has_value());

    session.mark_target_exited();
    REQUIRE(session.snapshot().status == SessionStatus::target_exited);
    REQUIRE(!session.snapshot().target.has_value());

    UiCommand status{
        .type = UiCommandType::get_status,
        .ui_epoch = "ui-2",
        .ui_sequence = 1,
        .target_generation = "new-page",
    };
    REQUIRE(session.accept(status) == CommandAcceptance::accepted);

    session.mark_webview_failed();
    REQUIRE(session.snapshot().status == SessionStatus::webview_failed);

    ControllerSession elevated_session;
    REQUIRE(elevated_session.accept(select) ==
            CommandAcceptance::accepted);
    elevated_session.mark_elevation_required();
    const UiCommand relaunch{
        .type = UiCommandType::relaunch_elevated,
        .ui_epoch = "ui-1",
        .ui_sequence = 2,
        .target_generation = "target-1",
    };
    REQUIRE(elevated_session.accept(relaunch) ==
            CommandAcceptance::accepted);
}

void controller_speed_queue_never_grows_past_two_slots() {
    ControllerSession session;
    REQUIRE(session.queue_speed(0.5, 1));
    for (std::uint64_t sequence = 2; sequence <= 1'000; ++sequence) {
        REQUIRE(!session.queue_speed(2.0, sequence));
    }
    const auto promoted = session.acknowledge_speed(1);
    REQUIRE(promoted.has_value());
    REQUIRE(promoted->ui_sequence == 1'000);
}

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

}  // namespace

int main() {
    constexpr std::array tests{
        TestCase{"trusted origin and exact schema",
                 &web_messages_require_trusted_origin_and_exact_schema},
        TestCase{"all command payload variants",
                 &all_command_payload_variants_are_strict},
        TestCase{"controller session ordering",
                 &controller_session_rejects_stale_ui_and_hook_state},
        TestCase{"bounded controller speed queue",
                 &controller_speed_queue_never_grows_past_two_slots},
    };

    std::size_t failures = 0;
    for (const auto& test : tests) {
        try {
            test.function();
            std::cout << "[pass] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[fail] " << test.name << ": " << error.what() << '\n';
        }
    }

    return failures == 0 ? 0 : 1;
}
