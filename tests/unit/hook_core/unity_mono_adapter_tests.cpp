#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

#include "timetransle/hook/unity_mono_adapter.hpp"

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(
    const bool condition,
    const std::string_view expression,
    const std::source_location location = std::source_location::current()) {
    if (!condition) {
        throw TestFailure{
            std::string{expression} + " at line " +
            std::to_string(location.line())};
    }
}

#define REQUIRE(expression) require((expression), #expression)

float actual_time_scale = 1.0F;
float delta_time = 0.016F;

float __cdecl fake_get_time_scale() {
    return actual_time_scale;
}

void __cdecl fake_set_time_scale(const float value) {
    actual_time_scale = value;
}

float __cdecl fake_get_delta_time() {
    return delta_time;
}

HMODULE fake_mono_module{};

template <typename Function>
Function resolve_fake_mono_export(const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(fake_mono_module, name));
}

void discovery_attaches_before_accessing_mono_thread_state() {
    using ResetFunction = void(__cdecl*)();
    using CountFunction = std::uint32_t(__cdecl*)();

    const auto reset = resolve_fake_mono_export<ResetFunction>(
        "TimeTransleFakeMonoReset");
    const auto thread_current_calls = resolve_fake_mono_export<CountFunction>(
        "TimeTransleFakeMonoThreadCurrentCalls");
    const auto thread_attach_calls = resolve_fake_mono_export<CountFunction>(
        "TimeTransleFakeMonoThreadAttachCalls");
    const auto thread_detach_calls = resolve_fake_mono_export<CountFunction>(
        "TimeTransleFakeMonoThreadDetachCalls");
    const auto assembly_foreach_calls = resolve_fake_mono_export<CountFunction>(
        "TimeTransleFakeMonoAssemblyForeachCalls");
    const auto order_violations = resolve_fake_mono_export<CountFunction>(
        "TimeTransleFakeMonoOrderViolations");
    REQUIRE(reset != nullptr);
    REQUIRE(thread_current_calls != nullptr);
    REQUIRE(thread_attach_calls != nullptr);
    REQUIRE(thread_detach_calls != nullptr);
    REQUIRE(assembly_foreach_calls != nullptr);
    REQUIRE(order_violations != nullptr);

    reset();
    const auto discovery = timetransle::hook::discover_unity_mono();
    REQUIRE(discovery.mono_runtime_found);
    REQUIRE(discovery.unity_time_found);
    REQUIRE(discovery.get_time_scale != nullptr);
    REQUIRE(discovery.set_time_scale != nullptr);
    REQUIRE(discovery.get_delta_time != nullptr);
    REQUIRE(thread_current_calls() == 0);
    REQUIRE(thread_attach_calls() == 1);
    REQUIRE(assembly_foreach_calls() == 1);
    REQUIRE(thread_detach_calls() == 1);
    REQUIRE(order_violations() == 0);
}

void multiplier_is_applied_on_the_main_thread() {
    actual_time_scale = 1.0F;
    timetransle::hook::UnityMonoAdapter adapter;
    REQUIRE(adapter.activate(
        &fake_get_time_scale,
        &fake_set_time_scale,
        &fake_get_delta_time,
        0));

    adapter.queue_external_state(2.0, false, 1, 7);
    REQUIRE(adapter.snapshot().effect_state ==
            timetransle::protocol::EffectState::pending);
    REQUIRE(actual_time_scale == 1.0F);
    REQUIRE(adapter.get_delta_time() == delta_time);
    REQUIRE(actual_time_scale == 2.0F);
    REQUIRE(adapter.get_time_scale() == 1.0F);
    REQUIRE(adapter.snapshot().applied_command_sequence == 7);
    REQUIRE(adapter.snapshot().effect_state ==
            timetransle::protocol::EffectState::applied);
}

void game_scale_and_external_pause_are_composed() {
    actual_time_scale = 1.0F;
    timetransle::hook::UnityMonoAdapter adapter;
    REQUIRE(adapter.activate(
        &fake_get_time_scale,
        &fake_set_time_scale,
        &fake_get_delta_time,
        0));

    adapter.queue_external_state(2.0, false, 1, 1);
    static_cast<void>(adapter.get_delta_time());
    adapter.set_time_scale(0.5F);
    REQUIRE(actual_time_scale == 1.0F);
    REQUIRE(adapter.get_time_scale() == 0.5F);

    adapter.queue_external_state(2.0, true, 2, 2);
    static_cast<void>(adapter.get_delta_time());
    REQUIRE(actual_time_scale == 0.0F);
    adapter.set_time_scale(0.25F);
    REQUIRE(actual_time_scale == 0.0F);
    REQUIRE(adapter.get_time_scale() == 0.25F);

    adapter.queue_external_state(2.0, false, 3, 3);
    static_cast<void>(adapter.get_delta_time());
    REQUIRE(actual_time_scale == 0.5F);
}

void invalid_values_do_not_reach_unity() {
    actual_time_scale = 0.75F;
    timetransle::hook::UnityMonoAdapter adapter;
    REQUIRE(adapter.activate(
        &fake_get_time_scale,
        &fake_set_time_scale,
        &fake_get_delta_time,
        0));
    REQUIRE(adapter.get_time_scale() == 0.75F);

    adapter.set_time_scale(std::numeric_limits<float>::infinity());
    REQUIRE(actual_time_scale == 0.75F);
    REQUIRE(adapter.snapshot().effect_state ==
            timetransle::protocol::EffectState::failed);
}

void degraded_mode_keeps_the_win32_backend() {
    timetransle::hook::UnityMonoAdapter adapter;
    adapter.mark_unity_detected_degraded();
    const auto state = adapter.snapshot();
    REQUIRE(state.target_runtime ==
            timetransle::protocol::TargetRuntime::unity_mono);
    REQUIRE(state.time_backend ==
            timetransle::protocol::TimeBackend::win32_clock);
    REQUIRE(state.effect_state ==
            timetransle::protocol::EffectState::degraded);
    REQUIRE(!adapter.uses_unity_backend());
}

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

}  // namespace

int main(const int argument_count, char** const arguments) {
    if (argument_count != 2) {
        std::cerr << "Expected the fake Mono runtime path.\n";
        return 2;
    }
    fake_mono_module = LoadLibraryA(arguments[1]);
    if (fake_mono_module == nullptr) {
        std::cerr << "Unable to load fake Mono runtime.\n";
        return 2;
    }

    constexpr std::array tests{
        TestCase{
            "discovery attaches before Mono access",
            &discovery_attaches_before_accessing_mono_thread_state},
        TestCase{"main-thread multiplier", &multiplier_is_applied_on_the_main_thread},
        TestCase{"game scale and pause composition", &game_scale_and_external_pause_are_composed},
        TestCase{"invalid values", &invalid_values_do_not_reach_unity},
        TestCase{"degraded fallback", &degraded_mode_keeps_the_win32_backend},
    };

    std::size_t failures{};
    for (const auto& test : tests) {
        try {
            test.function();
            std::cout << "[pass] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[fail] " << test.name << ": " << error.what() << '\n';
        }
    }
    FreeLibrary(fake_mono_module);
    return failures == 0 ? 0 : 1;
}
