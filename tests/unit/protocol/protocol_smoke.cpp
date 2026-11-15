#include <cstddef>
#include <iostream>
#include <string_view>

#include "timetransle/common/build_info.hpp"
#include "timetransle/protocol/constants.hpp"
#include "timetransle/protocol/error_code.hpp"

namespace {

int fail(const std::string_view message) {
    std::cerr << "protocol smoke failure: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using timetransle::protocol::ErrorCode;

    static_assert(timetransle::protocol::frame_magic[0] == std::byte{'T'});
    static_assert(timetransle::protocol::frame_magic[1] == std::byte{'T'});
    static_assert(timetransle::protocol::frame_magic[2] == std::byte{'L'});
    static_assert(timetransle::protocol::frame_magic[3] == std::byte{'E'});
    static_assert(timetransle::protocol::protocol_major == 1);
    static_assert(timetransle::protocol::frame_header_bytes == 64);
    static_assert(timetransle::protocol::max_payload_bytes == 64U * 1024U);
    static_assert(
        static_cast<std::uint32_t>(ErrorCode::elevation_failed) ==
        2012U);

    if (timetransle::protocol::to_string(ErrorCode::invalid_speed) !=
            "INVALID_SPEED" ||
        timetransle::protocol::to_string(
            ErrorCode::pipe_disconnected) != "PIPE_DISCONNECTED" ||
        timetransle::protocol::to_string(
            ErrorCode::elevation_failed) != "ELEVATION_FAILED") {
        return fail("stable error names changed");
    }

    constexpr std::string_view expected_architecture = sizeof(void*) == 8 ? "x64" : "x86";
    static_assert(timetransle::build_info::architecture == expected_architecture);
    static_assert(!timetransle::build_info::build_id.empty());

    std::cout << "TimeTransle protocol smoke passed for "
              << timetransle::build_info::architecture << " ("
              << timetransle::build_info::build_id << ")\n";
    return 0;
}
