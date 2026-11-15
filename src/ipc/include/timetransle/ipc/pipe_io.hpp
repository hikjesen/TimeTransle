#pragma once

#include <Windows.h>

#include "timetransle/ipc/protocol.hpp"

namespace timetransle::ipc {

enum class PipeIoResult {
    ok,
    disconnected,
    invalid_frame,
    io_error,
};

[[nodiscard]] PipeIoResult read_frame(HANDLE pipe, Frame& frame) noexcept;
[[nodiscard]] PipeIoResult write_frame(
    HANDLE pipe,
    const FrameHeader& header,
    std::span<const std::byte> payload) noexcept;

}  // namespace timetransle::ipc
