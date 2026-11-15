#include <windows.h>

#include <cstdint>
#include <limits>
#include <string_view>

namespace {

[[nodiscard]] bool parse_duration(
    const std::wstring_view text,
    DWORD& output) noexcept {
    if (text.empty() || text.size() > 8) {
        return false;
    }
    std::uint64_t value{};
    for (const auto character : text) {
        if (character < L'0' || character > L'9') {
            return false;
        }
        value = value * 10U +
                static_cast<std::uint64_t>(character - L'0');
        if (value >
            (std::numeric_limits<DWORD>::max)()) {
            return false;
        }
    }
    output = static_cast<DWORD>(value);
    return output > 0;
}

}  // namespace

int wmain(const int argument_count, wchar_t** const arguments) {
    if (argument_count != 3) {
        return 2;
    }
    DWORD duration{};
    if (!parse_duration(arguments[2], duration)) {
        return 3;
    }

    const auto fixture = LoadLibraryW(arguments[1]);
    if (fixture == nullptr) {
        return 4;
    }

    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamic_code{};
    dynamic_code.ProhibitDynamicCode = 1;
    if (SetProcessMitigationPolicy(
            ProcessDynamicCodePolicy,
            &dynamic_code,
            sizeof(dynamic_code)) == FALSE) {
        return 5;
    }

    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY signature{};
    signature.MicrosoftSignedOnly = 1;
    if (SetProcessMitigationPolicy(
            ProcessSignaturePolicy,
            &signature,
            sizeof(signature)) == FALSE) {
        return 6;
    }

    Sleep(duration);
    return 0;
}
