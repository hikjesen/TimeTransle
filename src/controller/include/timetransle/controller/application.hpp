#pragma once

#include <optional>

#include <windows.h>

#include "timetransle/safety/policy.hpp"

namespace timetransle::controller {

struct ControllerLaunchOptions {
    std::optional<safety::ProcessIdentity> elevated_target;
};

[[nodiscard]] int run_controller(
    HINSTANCE instance,
    int show_command,
    ControllerLaunchOptions options = {});

[[nodiscard]] int run_controller_self_test();

}  // namespace timetransle::controller
