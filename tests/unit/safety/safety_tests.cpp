#include <array>
#include <exception>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

#include "timetransle/safety/policy.hpp"

namespace {

using timetransle::protocol::ErrorCode;
using timetransle::safety::SafetyEvidence;

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

[[nodiscard]] constexpr SafetyEvidence safe_evidence() noexcept {
    return {
        .query_complete = true,
        .identity_matches = true,
        .architecture_supported = true,
        .injector_architecture_matches = true,
        .integrity_complete = true,
        .target_integrity_not_higher = true,
        .protection_check_complete = true,
        .protected_process = false,
        .critical_check_complete = true,
        .critical_process = false,
        .dll_identity_trusted = true,
    };
}

void complete_safe_evidence_is_allowed() {
    const auto decision = timetransle::safety::evaluate(safe_evidence());
    REQUIRE(decision.allowed);
    REQUIRE(decision.error == ErrorCode::ok);
}

void every_unknown_or_forbidden_signal_fails_closed() {
    struct Rejection {
        void (*mutate)(SafetyEvidence&) noexcept;
        ErrorCode expected;
    };

    constexpr std::array rejections{
        Rejection{[](SafetyEvidence& value) noexcept { value.query_complete = false; },
                  ErrorCode::access_denied},
        Rejection{[](SafetyEvidence& value) noexcept { value.identity_matches = false; },
                  ErrorCode::target_identity_changed},
        Rejection{[](SafetyEvidence& value) noexcept {
                      value.architecture_supported = false;
                  },
                  ErrorCode::architecture_unsupported},
        Rejection{[](SafetyEvidence& value) noexcept {
                      value.injector_architecture_matches = false;
                  },
                  ErrorCode::architecture_unsupported},
        Rejection{[](SafetyEvidence& value) noexcept {
                      value.integrity_complete = false;
                  },
                  ErrorCode::access_denied},
        Rejection{[](SafetyEvidence& value) noexcept {
                      value.target_integrity_not_higher = false;
                  },
                  ErrorCode::integrity_mismatch},
        Rejection{[](SafetyEvidence& value) noexcept {
                      value.protection_check_complete = false;
                  },
                  ErrorCode::access_denied},
        Rejection{[](SafetyEvidence& value) noexcept {
                      value.protected_process = true;
                  },
                  ErrorCode::protected_process},
        Rejection{[](SafetyEvidence& value) noexcept {
                      value.critical_check_complete = false;
                  },
                  ErrorCode::access_denied},
        Rejection{[](SafetyEvidence& value) noexcept {
                      value.critical_process = true;
                  },
                  ErrorCode::critical_process},
        Rejection{[](SafetyEvidence& value) noexcept {
                      value.dll_identity_trusted = false;
                  },
                  ErrorCode::dll_not_trusted},
    };

    for (const auto& rejection : rejections) {
        auto evidence = safe_evidence();
        rejection.mutate(evidence);
        const auto decision = timetransle::safety::evaluate(evidence);
        REQUIRE(!decision.allowed);
        REQUIRE(decision.error == rejection.expected);
    }
}

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

}  // namespace

int main() {
    constexpr std::array tests{
        TestCase{"complete safe evidence", &complete_safe_evidence_is_allowed},
        TestCase{"fail-closed rejection matrix",
                 &every_unknown_or_forbidden_signal_fails_closed},
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
        return 1;
    }
    std::cout << tests.size() << " safety tests passed\n";
    return 0;
}
