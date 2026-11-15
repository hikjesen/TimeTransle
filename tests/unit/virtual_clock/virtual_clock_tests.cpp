#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "timetransle/clock/virtual_clock.hpp"

namespace {

using timetransle::clock::ClockOrigins;
using timetransle::clock::RealQpcSource;
using timetransle::clock::UpdateResult;
using timetransle::clock::VirtualClock;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(
    const bool condition,
    const std::string_view expression,
    const std::source_location location = std::source_location::current()) {
    if (condition) {
        return;
    }

    throw TestFailure(
        std::string{location.file_name()} + ':' + std::to_string(location.line()) +
        " requirement failed: " + std::string{expression});
}

#define REQUIRE(expression) require((expression), #expression)

struct FakeQpc {
    std::atomic<std::int64_t> value{};

    [[nodiscard]] static std::int64_t read(void* const context) noexcept {
        return static_cast<FakeQpc*>(context)->value.load(std::memory_order_relaxed);
    }

    void set(const std::int64_t next) noexcept {
        value.store(next, std::memory_order_relaxed);
    }

    void advance(const std::int64_t delta) noexcept {
        value.fetch_add(delta, std::memory_order_relaxed);
    }

    [[nodiscard]] RealQpcSource source() noexcept {
        return {
            .read = &FakeQpc::read,
            .context = this,
        };
    }
};

struct IncrementingQpc {
    std::atomic<std::int64_t> value{};

    [[nodiscard]] static std::int64_t read(void* const context) noexcept {
        return static_cast<IncrementingQpc*>(context)->value.fetch_add(
            1,
            std::memory_order_relaxed);
    }

    [[nodiscard]] RealQpcSource source() noexcept {
        return {
            .read = &IncrementingQpc::read,
            .context = this,
        };
    }
};

[[nodiscard]] constexpr ClockOrigins origins(
    const std::int64_t qpc = 1'000,
    const std::uint64_t frequency = 1'000) noexcept {
    return {
        .qpc = qpc,
        .qpc_frequency = frequency,
        .tick_count64 = 20'000,
        .tick_count32 = 30'000,
        .time_get_time = 40'000,
    };
}

void initial_state_and_initialization_validation() {
    FakeQpc fake;
    fake.set(1'000);
    VirtualClock clock{origins(), fake.source()};

    REQUIRE(clock.is_ready());
    const auto state = clock.state();
    REQUIRE(state.configured_speed == 1.0);
    REQUIRE(!state.paused);
    REQUIRE(state.revision == 0);

    const auto readings = clock.read_all();
    REQUIRE(readings.qpc == 1'000);
    REQUIRE(readings.tick_count64 == 20'000);
    REQUIRE(readings.tick_count32 == 30'000);
    REQUIRE(readings.time_get_time == 40'000);

    const ClockOrigins invalid_origins{
        .qpc = 10,
        .qpc_frequency = 0,
        .tick_count64 = 20,
        .tick_count32 = 30,
        .time_get_time = 40,
    };
    VirtualClock invalid_frequency{invalid_origins, fake.source()};
    REQUIRE(!invalid_frequency.is_ready());
    REQUIRE(invalid_frequency.set_speed(2.0) == UpdateResult::not_initialized);
    REQUIRE(invalid_frequency.read_qpc() == 10);

    VirtualClock invalid_source{origins(), RealQpcSource{}};
    REQUIRE(!invalid_source.is_ready());
    REQUIRE(invalid_source.set_paused(true) == UpdateResult::not_initialized);
}

void speed_validation_rejects_non_finite_and_out_of_range_values() {
    FakeQpc fake;
    fake.set(1'000);
    VirtualClock clock{origins(), fake.source()};

    const std::array invalid_speeds{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -1.0,
        0.0,
        0.049999,
        8.000001,
    };

    for (const auto speed : invalid_speeds) {
        REQUIRE(!VirtualClock::is_valid_speed(speed));
        REQUIRE(clock.set_speed(speed) == UpdateResult::invalid_speed);
    }

    REQUIRE(clock.state().revision == 0);
    REQUIRE(clock.state().configured_speed == 1.0);
    REQUIRE(VirtualClock::is_valid_speed(0.05));
    REQUIRE(VirtualClock::is_valid_speed(8.0));
}

void supported_speeds_scale_fake_qpc_deterministically() {
    constexpr std::array speeds{0.05, 0.5, 1.0, 2.0, 8.0};

    for (const auto speed : speeds) {
        FakeQpc fake;
        fake.set(10'000);
        VirtualClock clock{origins(10'000, 1'000), fake.source()};
        REQUIRE(clock.set_speed(speed) == UpdateResult::applied);

        fake.advance(10'000);
        const auto expected = 10'000 + static_cast<std::int64_t>(10'000.0 * speed);
        REQUIRE(std::abs(clock.read_qpc() - expected) <= 1);
    }
}

void speed_changes_are_continuous() {
    FakeQpc fake;
    fake.set(1'000);
    VirtualClock clock{origins(), fake.source()};

    fake.advance(500);
    REQUIRE(clock.read_qpc() == 1'500);
    REQUIRE(clock.set_speed(0.5) == UpdateResult::applied);
    REQUIRE(clock.read_qpc() == 1'500);

    fake.advance(200);
    REQUIRE(clock.read_qpc() == 1'600);
    REQUIRE(clock.set_speed(2.0) == UpdateResult::applied);
    REQUIRE(clock.read_qpc() == 1'600);

    fake.advance(100);
    REQUIRE(clock.read_qpc() == 1'800);
    REQUIRE(clock.state().revision == 2);
}

void every_supported_speed_pair_reanchors_continuously() {
    constexpr std::array speeds{0.05, 0.5, 1.0, 2.0, 8.0};

    for (const auto old_speed : speeds) {
        for (const auto new_speed : speeds) {
            FakeQpc fake;
            fake.set(10'000);
            VirtualClock clock{origins(10'000, 1'000), fake.source()};

            REQUIRE(clock.set_speed(old_speed) == UpdateResult::applied);
            fake.advance(137);
            const auto boundary = clock.read_qpc();

            REQUIRE(clock.set_speed(new_speed) == UpdateResult::applied);
            REQUIRE(std::abs(clock.read_qpc() - boundary) <= 1);

            fake.advance(211);
            const auto expected_delta =
                static_cast<std::int64_t>(211.0 * new_speed);
            REQUIRE(std::abs(clock.read_qpc() - (boundary + expected_delta)) <= 1);
        }
    }
}

void pause_resume_and_reset_preserve_continuity() {
    FakeQpc fake;
    fake.set(1'000);
    VirtualClock clock{origins(), fake.source()};

    REQUIRE(clock.set_speed(2.0) == UpdateResult::applied);
    fake.advance(100);
    REQUIRE(clock.read_qpc() == 1'200);

    REQUIRE(clock.set_paused(true) == UpdateResult::applied);
    const auto frozen = clock.read_qpc();
    fake.advance(1'000);
    REQUIRE(clock.read_qpc() == frozen);

    REQUIRE(clock.set_speed(0.5) == UpdateResult::applied);
    fake.advance(500);
    REQUIRE(clock.read_qpc() == frozen);
    REQUIRE(clock.state().configured_speed == 0.5);
    REQUIRE(clock.state().paused);

    REQUIRE(clock.set_paused(false) == UpdateResult::applied);
    REQUIRE(clock.read_qpc() == frozen);
    fake.advance(100);
    REQUIRE(clock.read_qpc() == frozen + 50);

    REQUIRE(clock.set_paused(true) == UpdateResult::applied);
    fake.advance(100);
    const auto reset_boundary = clock.read_qpc();
    REQUIRE(clock.reset() == UpdateResult::applied);
    REQUIRE(clock.read_qpc() == reset_boundary);
    REQUIRE(clock.state().configured_speed == 1.0);
    REQUIRE(!clock.state().paused);
    fake.advance(100);
    REQUIRE(clock.read_qpc() == reset_boundary + 100);
}

void all_api_readings_share_one_elapsed_timeline() {
    FakeQpc fake;
    fake.set(100);
    const ClockOrigins custom_origins{
        .qpc = 100,
        .qpc_frequency = 3,
        .tick_count64 = 10'000,
        .tick_count32 = 20'000,
        .time_get_time = 30'000,
    };
    VirtualClock clock{custom_origins, fake.source()};

    fake.advance(5);
    const auto readings = clock.read_all();
    constexpr std::uint64_t expected_elapsed_milliseconds = 1'666;
    REQUIRE(readings.qpc == 105);
    REQUIRE(readings.tick_count64 ==
            custom_origins.tick_count64 + expected_elapsed_milliseconds);
    REQUIRE(readings.tick_count32 ==
            custom_origins.tick_count32 + expected_elapsed_milliseconds);
    REQUIRE(readings.time_get_time ==
            custom_origins.time_get_time + expected_elapsed_milliseconds);
}

void dword_timers_keep_natural_wrap_semantics() {
    FakeQpc fake;
    fake.set(0);
    const ClockOrigins wrap_origins{
        .qpc = 0,
        .qpc_frequency = 1'000,
        .tick_count64 = 100,
        .tick_count32 = std::numeric_limits<std::uint32_t>::max() - 2U,
        .time_get_time = std::numeric_limits<std::uint32_t>::max() - 1U,
    };
    VirtualClock clock{wrap_origins, fake.source()};

    fake.advance(5);
    const auto readings = clock.read_all();
    REQUIRE(readings.tick_count64 == 105);
    REQUIRE(readings.tick_count32 == 2);
    REQUIRE(readings.time_get_time == 3);
}

void wide_conversion_and_overflow_paths_are_defined() {
    FakeQpc fake;
    fake.set(0);
    constexpr auto signed_max = std::numeric_limits<std::int64_t>::max();
    constexpr auto unsigned_max = std::numeric_limits<std::uint64_t>::max();
    const ClockOrigins wide_origins{
        .qpc = 0,
        .qpc_frequency = static_cast<std::uint64_t>(signed_max),
        .tick_count64 = unsigned_max - 500U,
        .tick_count32 = std::numeric_limits<std::uint32_t>::max() - 500U,
        .time_get_time = std::numeric_limits<std::uint32_t>::max() - 250U,
    };
    VirtualClock clock{wide_origins, fake.source()};

    fake.set(signed_max);
    auto readings = clock.read_all();
    REQUIRE(readings.qpc == signed_max);
    REQUIRE(readings.tick_count64 == 499);
    REQUIRE(readings.tick_count32 == 499);
    REQUIRE(readings.time_get_time == 749);

    fake.set(0);
    VirtualClock saturating_clock{wide_origins, fake.source()};
    REQUIRE(saturating_clock.set_speed(8.0) == UpdateResult::applied);
    fake.set(signed_max);
    readings = saturating_clock.read_all();
    REQUIRE(readings.qpc == signed_max);
}

void thirty_two_readers_never_observe_per_thread_regression() {
    IncrementingQpc fake;
    VirtualClock clock{origins(0, 1'000'000), fake.source()};

    constexpr std::size_t reader_count = 32;
    constexpr std::size_t reads_per_reader = 10'000;
    constexpr std::size_t writer_cycles = 500;

    std::atomic<bool> start{};
    std::atomic<std::uint32_t> failures{};
    std::atomic<bool> writer_failed{};
    std::vector<std::thread> readers;
    readers.reserve(reader_count);

    for (std::size_t index = 0; index < reader_count; ++index) {
        readers.emplace_back([&clock, &start, &failures] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            auto previous_qpc = clock.read_qpc();
            auto previous_revision = clock.state().revision;
            for (std::size_t read_index = 0; read_index < reads_per_reader;
                 ++read_index) {
                const auto current_qpc = clock.read_qpc();
                const auto current_state = clock.state();
                const auto revision_phase = current_state.revision % 4U;
                const auto snapshot_is_consistent =
                    current_state.revision == 0
                        ? current_state.configured_speed == 1.0 &&
                              !current_state.paused
                    : revision_phase == 1
                        ? current_state.configured_speed == 0.5 &&
                              !current_state.paused
                    : revision_phase == 2
                        ? current_state.configured_speed == 0.5 &&
                              current_state.paused
                    : revision_phase == 3
                        ? current_state.configured_speed == 2.0 &&
                              current_state.paused
                        : current_state.configured_speed == 2.0 &&
                              !current_state.paused;

                if (current_qpc < previous_qpc ||
                    current_state.revision < previous_revision ||
                    !snapshot_is_consistent) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                previous_qpc = current_qpc;
                previous_revision = current_state.revision;
            }
        });
    }

    std::thread writer{[&clock, &start, &writer_failed] {
        start.store(true, std::memory_order_release);
        for (std::size_t cycle = 0; cycle < writer_cycles; ++cycle) {
            if (clock.set_speed(0.5) != UpdateResult::applied ||
                clock.set_paused(true) != UpdateResult::applied ||
                clock.set_speed(2.0) != UpdateResult::applied ||
                clock.set_paused(false) != UpdateResult::applied) {
                writer_failed.store(true, std::memory_order_relaxed);
                return;
            }
        }
    }};

    writer.join();
    for (auto& reader : readers) {
        reader.join();
    }

    REQUIRE(!writer_failed.load(std::memory_order_relaxed));
    REQUIRE(failures.load(std::memory_order_relaxed) == 0);
}

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

}  // namespace

int main() {
    constexpr std::array tests{
        TestCase{"initial state and initialization validation",
                 &initial_state_and_initialization_validation},
        TestCase{"speed validation",
                 &speed_validation_rejects_non_finite_and_out_of_range_values},
        TestCase{"supported speed scaling",
                 &supported_speeds_scale_fake_qpc_deterministically},
        TestCase{"continuous speed changes", &speed_changes_are_continuous},
        TestCase{"all supported speed transitions",
                 &every_supported_speed_pair_reanchors_continuously},
        TestCase{"pause, resume, and reset", &pause_resume_and_reset_preserve_continuity},
        TestCase{"unified API elapsed timeline",
                 &all_api_readings_share_one_elapsed_timeline},
        TestCase{"DWORD timer wrap", &dword_timers_keep_natural_wrap_semantics},
        TestCase{"wide conversion and overflow",
                 &wide_conversion_and_overflow_paths_are_defined},
        TestCase{"32-reader monotonic stress",
                 &thirty_two_readers_never_observe_per_thread_regression},
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

    if (failures != 0) {
        std::cerr << failures << " virtual clock test(s) failed\n";
        return 1;
    }

    std::cout << tests.size() << " virtual clock tests passed\n";
    return 0;
}
