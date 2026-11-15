#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace timetransle::ipc {

struct SpeedCommand {
    double speed{1.0};
    std::uint64_t ui_sequence{};
};

class BoundedSpeedCommandQueue final {
public:
    // Returns true when the command becomes the single in-flight command.
    // Otherwise it replaces the one pending command.
    [[nodiscard]] bool submit(const SpeedCommand command) noexcept {
        if (!in_flight_.has_value()) {
            in_flight_ = command;
            return true;
        }

        pending_ = command;
        return false;
    }

    // Completes the matching in-flight command and promotes the latest pending
    // value, if any. A stale acknowledgement changes nothing.
    [[nodiscard]] std::optional<SpeedCommand> acknowledge(
        const std::uint64_t ui_sequence) noexcept {
        if (!in_flight_.has_value() ||
            in_flight_->ui_sequence != ui_sequence) {
            return std::nullopt;
        }

        in_flight_.reset();
        if (!pending_.has_value()) {
            return std::nullopt;
        }

        in_flight_ = pending_;
        pending_.reset();
        return in_flight_;
    }

    [[nodiscard]] const std::optional<SpeedCommand>& in_flight() const noexcept {
        return in_flight_;
    }

    [[nodiscard]] const std::optional<SpeedCommand>& pending() const noexcept {
        return pending_;
    }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept {
        return 2;
    }

    void reset() noexcept {
        in_flight_.reset();
        pending_.reset();
    }

private:
    std::optional<SpeedCommand> in_flight_;
    std::optional<SpeedCommand> pending_;
};

}  // namespace timetransle::ipc
