#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "timetransle/hook/hook_installer.hpp"

namespace {

using timetransle::hook::HookBackend;
using timetransle::hook::HookInstaller;
using timetransle::hook::HookSpec;
using timetransle::hook::InstallResult;

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

enum class FailurePoint {
    none,
    initialize,
    first_create,
    second_create,
    prepare,
    first_queue,
    second_queue,
    apply,
};

struct FakeBackend {
    FailurePoint failure{};
    std::size_t create_calls{};
    std::size_t queue_calls{};
    std::size_t disable_calls{};
    std::size_t remove_calls{};
    std::size_t uninitialize_calls{};
    std::array<void*, 4> removed_targets{};

    [[nodiscard]] static FakeBackend& from(void* const context) noexcept {
        return *static_cast<FakeBackend*>(context);
    }

    [[nodiscard]] static bool initialize(void* const context) noexcept {
        return from(context).failure != FailurePoint::initialize;
    }

    [[nodiscard]] static bool create(
        void* const context,
        void* const target,
        void*,
        void** const original) noexcept {
        auto& backend = from(context);
        ++backend.create_calls;
        if ((backend.create_calls == 1 &&
             backend.failure == FailurePoint::first_create) ||
            (backend.create_calls == 2 &&
             backend.failure == FailurePoint::second_create)) {
            return false;
        }

        *original = target;
        return true;
    }

    [[nodiscard]] static bool queue_enable(
        void* const context,
        void*) noexcept {
        auto& backend = from(context);
        ++backend.queue_calls;
        return !((backend.queue_calls == 1 &&
                  backend.failure == FailurePoint::first_queue) ||
                 (backend.queue_calls == 2 &&
                  backend.failure == FailurePoint::second_queue));
    }

    [[nodiscard]] static bool apply_queued(void* const context) noexcept {
        return from(context).failure != FailurePoint::apply;
    }

    static void disable_all(void* const context) noexcept {
        ++from(context).disable_calls;
    }

    static void remove(void* const context, void* const target) noexcept {
        auto& backend = from(context);
        backend.removed_targets[backend.remove_calls] = target;
        ++backend.remove_calls;
    }

    static void uninitialize(void* const context) noexcept {
        ++from(context).uninitialize_calls;
    }

    [[nodiscard]] HookBackend api() noexcept {
        return {
            .context = this,
            .initialize = &FakeBackend::initialize,
            .create = &FakeBackend::create,
            .queue_enable = &FakeBackend::queue_enable,
            .apply_queued = &FakeBackend::apply_queued,
            .disable_all = &FakeBackend::disable_all,
            .remove = &FakeBackend::remove,
            .uninitialize = &FakeBackend::uninitialize,
        };
    }
};

struct Fixture {
    int target_one{};
    int target_two{};
    int detour_one{};
    int detour_two{};
    void* original_one{};
    void* original_two{};
    std::array<HookSpec, 2> hooks;

    Fixture()
        : hooks{
              HookSpec{&target_one, &detour_one, &original_one},
              HookSpec{&target_two, &detour_two, &original_two},
          } {}
};

[[nodiscard]] bool prepare(void* const context) noexcept {
    const auto failure = *static_cast<const FailurePoint*>(context);
    return failure != FailurePoint::prepare;
}

void successful_install_is_all_or_nothing() {
    FakeBackend backend;
    Fixture fixture;
    HookInstaller installer{backend.api()};
    const auto failure = FailurePoint::none;

    REQUIRE(installer.install(fixture.hooks, &prepare, const_cast<FailurePoint*>(&failure)) ==
            InstallResult::installed);
    REQUIRE(installer.is_installed());
    REQUIRE(backend.create_calls == 2);
    REQUIRE(backend.queue_calls == 2);
    REQUIRE(backend.disable_calls == 0);
    REQUIRE(backend.remove_calls == 0);
    REQUIRE(backend.uninitialize_calls == 0);
    REQUIRE(fixture.original_one == &fixture.target_one);
    REQUIRE(fixture.original_two == &fixture.target_two);
    REQUIRE(installer.install(fixture.hooks, &prepare, const_cast<FailurePoint*>(&failure)) ==
            InstallResult::already_installed);
}

void every_failure_rolls_back_created_hooks() {
    constexpr std::array failure_points{
        FailurePoint::first_create,
        FailurePoint::second_create,
        FailurePoint::prepare,
        FailurePoint::first_queue,
        FailurePoint::second_queue,
        FailurePoint::apply,
    };

    for (const auto failure : failure_points) {
        FakeBackend backend;
        backend.failure = failure;
        Fixture fixture;
        HookInstaller installer{backend.api()};

        REQUIRE(installer.install(
                    fixture.hooks,
                    &prepare,
                    &backend.failure) != InstallResult::installed);
        REQUIRE(!installer.is_installed());
        REQUIRE(backend.disable_calls == 1);
        REQUIRE(backend.uninitialize_calls == 1);

        const auto expected_created =
            failure == FailurePoint::first_create ? std::size_t{0}
            : failure == FailurePoint::second_create
                ? std::size_t{1}
                : std::size_t{2};
        REQUIRE(backend.remove_calls == expected_created);
        if (expected_created == 2) {
            REQUIRE(backend.removed_targets[0] == &fixture.target_two);
            REQUIRE(backend.removed_targets[1] == &fixture.target_one);
        }
    }
}

void invalid_and_duplicate_specs_are_rejected_before_initialization() {
    FakeBackend backend;
    Fixture fixture;
    const auto failure = FailurePoint::none;

    HookInstaller duplicate_installer{backend.api()};
    fixture.hooks[1].target = fixture.hooks[0].target;
    REQUIRE(duplicate_installer.install(
                fixture.hooks,
                &prepare,
                const_cast<FailurePoint*>(&failure)) ==
            InstallResult::duplicate_target);
    REQUIRE(backend.create_calls == 0);
    REQUIRE(backend.uninitialize_calls == 0);

    HookInstaller invalid_installer{HookBackend{}};
    REQUIRE(invalid_installer.install(
                fixture.hooks,
                &prepare,
                const_cast<FailurePoint*>(&failure)) ==
            InstallResult::invalid_backend);
}

void failed_optional_install_can_retry_with_fallback_hooks() {
    FakeBackend backend;
    backend.failure = FailurePoint::apply;
    Fixture fixture;
    HookInstaller installer{backend.api()};
    auto prepare_result = FailurePoint::none;

    REQUIRE(installer.install(fixture.hooks, &prepare, &prepare_result) ==
            InstallResult::apply_failed);
    REQUIRE(!installer.is_installed());
    REQUIRE(backend.disable_calls == 1);
    REQUIRE(backend.remove_calls == 2);
    REQUIRE(backend.uninitialize_calls == 1);

    backend.failure = FailurePoint::none;
    backend.create_calls = 0;
    backend.queue_calls = 0;
    REQUIRE(installer.install(
                std::span<const HookSpec>{fixture.hooks}.first(1),
                &prepare,
                &prepare_result) == InstallResult::installed);
    REQUIRE(installer.is_installed());
}

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

}  // namespace

int main() {
    constexpr std::array tests{
        TestCase{"successful all-or-nothing install", &successful_install_is_all_or_nothing},
        TestCase{"rollback at every failure point", &every_failure_rolls_back_created_hooks},
        TestCase{"invalid and duplicate specs",
                 &invalid_and_duplicate_specs_are_rejected_before_initialization},
        TestCase{"fallback retry after rollback",
                 &failed_optional_install_can_retry_with_fallback_hooks},
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
        std::cerr << failures << " hook installer test(s) failed\n";
        return 1;
    }

    std::cout << tests.size() << " hook installer tests passed\n";
    return 0;
}
