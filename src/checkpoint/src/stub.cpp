#include "timetransle/checkpoint/runtime.hpp"
namespace timetransle::checkpoint {
bool initialize(clock::VirtualClock*) noexcept { return true; }
ipc::CheckpointPayload state() noexcept { return {}; }
protocol::ErrorCode execute(bool) noexcept { return protocol::ErrorCode::checkpoint_unavailable; }
void clear() noexcept {}
}

