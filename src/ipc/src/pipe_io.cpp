#include "timetransle/ipc/pipe_io.hpp"

#include <array>

namespace timetransle::ipc {
namespace {

[[nodiscard]] PipeIoResult classify_error(const DWORD error) noexcept {
    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED ||
        error == ERROR_NO_DATA || error == ERROR_HANDLE_EOF) {
        return PipeIoResult::disconnected;
    }
    return PipeIoResult::io_error;
}

[[nodiscard]] PipeIoResult read_exact(
    const HANDLE pipe,
    const std::span<std::byte> output) noexcept {
    std::size_t completed = 0;
    while (completed < output.size()) {
        DWORD bytes_read{};
        const auto remaining = output.size() - completed;
        const auto request = static_cast<DWORD>(remaining);
        if (ReadFile(
                pipe,
                output.data() + completed,
                request,
                &bytes_read,
                nullptr) == FALSE) {
            return classify_error(GetLastError());
        }
        if (bytes_read == 0) {
            return PipeIoResult::disconnected;
        }
        completed += bytes_read;
    }
    return PipeIoResult::ok;
}

[[nodiscard]] PipeIoResult write_exact(
    const HANDLE pipe,
    const std::span<const std::byte> input) noexcept {
    std::size_t completed = 0;
    while (completed < input.size()) {
        DWORD bytes_written{};
        const auto remaining = input.size() - completed;
        const auto request = static_cast<DWORD>(remaining);
        if (WriteFile(
                pipe,
                input.data() + completed,
                request,
                &bytes_written,
                nullptr) == FALSE) {
            return classify_error(GetLastError());
        }
        if (bytes_written == 0) {
            return PipeIoResult::disconnected;
        }
        completed += bytes_written;
    }
    return PipeIoResult::ok;
}

}  // namespace

PipeIoResult read_frame(const HANDLE pipe, Frame& frame) noexcept {
    std::array<std::byte, protocol::frame_header_bytes> header_bytes{};
    const auto header_result = read_exact(pipe, header_bytes);
    if (header_result != PipeIoResult::ok) {
        return header_result;
    }

    if (parse_header(header_bytes, frame.header) != HeaderParseResult::ok) {
        return PipeIoResult::invalid_frame;
    }

    return read_exact(
        pipe,
        std::span{frame.payload}.first(frame.header.payload_bytes));
}

PipeIoResult write_frame(
    const HANDLE pipe,
    const FrameHeader& header,
    const std::span<const std::byte> payload) noexcept {
    if (payload.size() != header.payload_bytes ||
        payload.size() > protocol::max_payload_bytes) {
        return PipeIoResult::invalid_frame;
    }

    const auto header_bytes = encode_header(header);
    const auto header_result = write_exact(pipe, header_bytes);
    if (header_result != PipeIoResult::ok) {
        return header_result;
    }

    return write_exact(pipe, payload);
}

}  // namespace timetransle::ipc
