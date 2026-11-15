#pragma once
#include "timetransle/ipc/protocol.hpp"
#include "timetransle/clock/virtual_clock.hpp"
namespace timetransle::checkpoint {
bool initialize(clock::VirtualClock* clock) noexcept;
ipc::CheckpointPayload state() noexcept;
protocol::ErrorCode execute(bool load) noexcept;
void clear() noexcept;
}

