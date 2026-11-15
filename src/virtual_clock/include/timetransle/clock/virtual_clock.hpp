#pragma once

#include <atomic>
#include <cstdint>

namespace timetransle::clock {

inline constexpr double minimum_speed = 0.05;
inline constexpr double maximum_speed = 8.0;
inline constexpr double initial_speed = 1.0;

using ReadRealQpc = std::int64_t (*)(void* context) noexcept;

struct RealQpcSource {
    ReadRealQpc read{};
    void* context{};
};

struct ClockOrigins {
    std::int64_t qpc{};
    std::uint64_t qpc_frequency{};
    std::uint64_t tick_count64{};
    std::uint32_t tick_count32{};
    std::uint32_t time_get_time{};
};

struct ClockState {
    double configured_speed{initial_speed};
    bool paused{};
    std::uint64_t revision{};
};

struct ClockReadings {
    std::int64_t qpc{};
    std::uint64_t tick_count64{};
    std::uint32_t tick_count32{};
    std::uint32_t time_get_time{};
};

enum class UpdateResult : std::uint8_t {
    applied,
    invalid_speed,
    not_initialized,
};

class VirtualClock final {
public:
    VirtualClock(ClockOrigins origins, RealQpcSource real_qpc_source) noexcept;

    VirtualClock(const VirtualClock&) = delete;
    VirtualClock& operator=(const VirtualClock&) = delete;
    VirtualClock(VirtualClock&&) = delete;
    VirtualClock& operator=(VirtualClock&&) = delete;

    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] static bool is_valid_speed(double speed) noexcept;

    // All mutating calls belong to one serialized writer.
    [[nodiscard]] UpdateResult set_speed(double speed) noexcept;
    [[nodiscard]] UpdateResult set_paused(bool paused) noexcept;
    [[nodiscard]] UpdateResult reset() noexcept;
    // Explicit checkpoint restore: control revisions keep advancing.
    [[nodiscard]] UpdateResult restore_qpc(std::int64_t qpc, double speed, bool paused) noexcept;

    // Readers are lock-free with respect to the single writer and perform no allocation.
    [[nodiscard]] ClockState state() const noexcept;
    [[nodiscard]] ClockReadings read_all() const noexcept;
    [[nodiscard]] std::int64_t read_qpc() const noexcept;
    [[nodiscard]] std::uint64_t read_tick_count64() const noexcept;
    [[nodiscard]] std::uint32_t read_tick_count32() const noexcept;
    [[nodiscard]] std::uint32_t read_time_get_time() const noexcept;

private:
    struct Snapshot {
        std::int64_t real_anchor_qpc{};
        std::int64_t virtual_anchor_qpc{};
        double configured_speed{initial_speed};
        bool paused{};
        std::uint64_t revision{};
    };

    [[nodiscard]] Snapshot load_snapshot() const noexcept;
    [[nodiscard]] std::int64_t read_virtual_qpc() const noexcept;
    [[nodiscard]] std::int64_t read_real_qpc() const noexcept;
    [[nodiscard]] UpdateResult publish(double speed, bool paused) noexcept;

    ClockOrigins origins_;
    RealQpcSource real_qpc_source_;
    bool ready_{};

    mutable std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::int64_t> real_anchor_qpc_;
    std::atomic<std::int64_t> virtual_anchor_qpc_;
    std::atomic<std::uint64_t> speed_bits_;
    std::atomic<std::uint32_t> flags_{0};
    std::atomic<std::uint64_t> revision_{0};
};

}  // namespace timetransle::clock
