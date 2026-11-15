#include "timetransle/clock/virtual_clock.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace timetransle::clock {
namespace {

constexpr std::uint32_t paused_flag = 1U;
constexpr std::uint64_t milliseconds_per_second = 1'000U;

[[nodiscard]] constexpr std::uint64_t speed_to_bits(const double speed) noexcept {
    return std::bit_cast<std::uint64_t>(speed);
}

[[nodiscard]] constexpr double speed_from_bits(const std::uint64_t bits) noexcept {
    return std::bit_cast<double>(bits);
}

[[nodiscard]] constexpr std::uint64_t nonnegative_delta(
    const std::int64_t now,
    const std::int64_t anchor) noexcept {
    if (now <= anchor) {
        return 0;
    }

    return static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(anchor);
}

[[nodiscard]] std::int64_t add_scaled_delta(
    const std::int64_t virtual_anchor,
    const std::uint64_t real_delta,
    const double speed) noexcept {
    const auto headroom =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) -
        static_cast<std::uint64_t>(virtual_anchor);
    const auto scaled =
        static_cast<long double>(real_delta) * static_cast<long double>(speed);

    if (scaled >= static_cast<long double>(headroom)) {
        return std::numeric_limits<std::int64_t>::max();
    }

    const auto scaled_ticks = static_cast<std::uint64_t>(scaled);
    const auto result_bits = static_cast<std::uint64_t>(virtual_anchor) + scaled_ticks;
    return std::bit_cast<std::int64_t>(result_bits);
}

struct ModularSum {
    std::uint64_t remainder{};
    std::uint64_t carry{};
};

[[nodiscard]] constexpr ModularSum add_modulo(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::uint64_t divisor) noexcept {
    if (left >= divisor - right) {
        return {
            .remainder = left - (divisor - right),
            .carry = 1,
        };
    }

    return {
        .remainder = left + right,
        .carry = 0,
    };
}

// Computes floor(remainder * multiplier / divisor) exactly without a wide
// intermediate. The caller guarantees remainder < divisor.
[[nodiscard]] constexpr std::uint64_t multiply_remainder_divide(
    const std::uint64_t remainder,
    const std::uint32_t multiplier,
    const std::uint64_t divisor) noexcept {
    std::uint64_t quotient = 0;
    std::uint64_t accumulated_remainder = 0;

    for (int bit = 31; bit >= 0; --bit) {
        quotient *= 2;
        const auto doubled =
            add_modulo(accumulated_remainder, accumulated_remainder, divisor);
        accumulated_remainder = doubled.remainder;
        quotient += doubled.carry;

        if ((multiplier & (std::uint32_t{1} << bit)) != 0U) {
            const auto with_term =
                add_modulo(accumulated_remainder, remainder, divisor);
            accumulated_remainder = with_term.remainder;
            quotient += with_term.carry;
        }
    }

    return quotient;
}

[[nodiscard]] constexpr std::uint64_t qpc_delta_to_milliseconds(
    const std::uint64_t qpc_delta,
    const std::uint64_t qpc_frequency) noexcept {
    const auto whole_seconds = qpc_delta / qpc_frequency;
    const auto qpc_remainder = qpc_delta % qpc_frequency;
    const auto fractional_milliseconds = multiply_remainder_divide(
        qpc_remainder,
        static_cast<std::uint32_t>(milliseconds_per_second),
        qpc_frequency);

    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (whole_seconds > (maximum - fractional_milliseconds) /
                            milliseconds_per_second) {
        return maximum;
    }

    return whole_seconds * milliseconds_per_second + fractional_milliseconds;
}

[[nodiscard]] std::int64_t virtual_qpc_at(
    const std::int64_t real_anchor_qpc,
    const std::int64_t virtual_anchor_qpc,
    const double configured_speed,
    const bool paused,
    const std::int64_t real_now) noexcept {
    if (paused) {
        return virtual_anchor_qpc;
    }

    return add_scaled_delta(
        virtual_anchor_qpc,
        nonnegative_delta(real_now, real_anchor_qpc),
        configured_speed);
}

}  // namespace

VirtualClock::VirtualClock(
    const ClockOrigins origins,
    const RealQpcSource real_qpc_source) noexcept
    : origins_(origins),
      real_qpc_source_(real_qpc_source),
      ready_(origins.qpc_frequency != 0 && real_qpc_source.read != nullptr),
      real_anchor_qpc_(origins.qpc),
      virtual_anchor_qpc_(origins.qpc),
      speed_bits_(speed_to_bits(initial_speed)) {}

bool VirtualClock::is_ready() const noexcept {
    return ready_;
}

bool VirtualClock::is_valid_speed(const double speed) noexcept {
    return std::isfinite(speed) && speed >= minimum_speed && speed <= maximum_speed;
}

UpdateResult VirtualClock::set_speed(const double speed) noexcept {
    if (!is_valid_speed(speed)) {
        return UpdateResult::invalid_speed;
    }

    if (!ready_) {
        return UpdateResult::not_initialized;
    }

    const auto current = load_snapshot();
    return publish(speed, current.paused);
}

UpdateResult VirtualClock::set_paused(const bool paused) noexcept {
    if (!ready_) {
        return UpdateResult::not_initialized;
    }

    const auto current = load_snapshot();
    return publish(current.configured_speed, paused);
}

UpdateResult VirtualClock::reset() noexcept {
    if (!ready_) {
        return UpdateResult::not_initialized;
    }

    return publish(initial_speed, false);
}

UpdateResult VirtualClock::restore_qpc(const std::int64_t qpc, const double speed, const bool paused) noexcept {
    if (!ready_) return UpdateResult::not_initialized;
    if (!is_valid_speed(speed)) return UpdateResult::invalid_speed;
    sequence_.fetch_add(1, std::memory_order_acq_rel);
    real_anchor_qpc_.store(read_real_qpc(), std::memory_order_relaxed);
    virtual_anchor_qpc_.store(qpc, std::memory_order_relaxed);
    speed_bits_.store(speed_to_bits(speed), std::memory_order_relaxed);
    flags_.store(paused ? paused_flag : 0U, std::memory_order_relaxed);
    revision_.fetch_add(1, std::memory_order_relaxed);
    sequence_.fetch_add(1, std::memory_order_release);
    return UpdateResult::applied;
}

ClockState VirtualClock::state() const noexcept {
    const auto snapshot = load_snapshot();
    return {
        .configured_speed = snapshot.configured_speed,
        .paused = snapshot.paused,
        .revision = snapshot.revision,
    };
}

ClockReadings VirtualClock::read_all() const noexcept {
    if (!ready_) {
        return {
            .qpc = origins_.qpc,
            .tick_count64 = origins_.tick_count64,
            .tick_count32 = origins_.tick_count32,
            .time_get_time = origins_.time_get_time,
        };
    }

    const auto virtual_qpc = read_virtual_qpc();
    const auto elapsed_qpc = nonnegative_delta(virtual_qpc, origins_.qpc);
    const auto elapsed_milliseconds =
        qpc_delta_to_milliseconds(elapsed_qpc, origins_.qpc_frequency);

    return {
        .qpc = virtual_qpc,
        .tick_count64 = origins_.tick_count64 + elapsed_milliseconds,
        .tick_count32 =
            origins_.tick_count32 + static_cast<std::uint32_t>(elapsed_milliseconds),
        .time_get_time =
            origins_.time_get_time + static_cast<std::uint32_t>(elapsed_milliseconds),
    };
}

std::int64_t VirtualClock::read_qpc() const noexcept {
    return read_virtual_qpc();
}

std::uint64_t VirtualClock::read_tick_count64() const noexcept {
    return read_all().tick_count64;
}

std::uint32_t VirtualClock::read_tick_count32() const noexcept {
    return read_all().tick_count32;
}

std::uint32_t VirtualClock::read_time_get_time() const noexcept {
    return read_all().time_get_time;
}

VirtualClock::Snapshot VirtualClock::load_snapshot() const noexcept {
    for (;;) {
        const auto sequence_before = sequence_.load(std::memory_order_acquire);
        if ((sequence_before & 1U) != 0U) {
            continue;
        }

        const Snapshot snapshot{
            .real_anchor_qpc = real_anchor_qpc_.load(std::memory_order_relaxed),
            .virtual_anchor_qpc = virtual_anchor_qpc_.load(std::memory_order_relaxed),
            .configured_speed =
                speed_from_bits(speed_bits_.load(std::memory_order_relaxed)),
            .paused = (flags_.load(std::memory_order_relaxed) & paused_flag) != 0U,
            .revision = revision_.load(std::memory_order_relaxed),
        };

        const auto sequence_after = sequence_.load(std::memory_order_acquire);
        if (sequence_before == sequence_after && (sequence_after & 1U) == 0U) {
            return snapshot;
        }
    }
}

std::int64_t VirtualClock::read_virtual_qpc() const noexcept {
    if (!ready_) {
        return origins_.qpc;
    }

    for (;;) {
        const auto sequence_before = sequence_.load(std::memory_order_acquire);
        if ((sequence_before & 1U) != 0U) {
            continue;
        }

        const Snapshot snapshot{
            .real_anchor_qpc = real_anchor_qpc_.load(std::memory_order_relaxed),
            .virtual_anchor_qpc = virtual_anchor_qpc_.load(std::memory_order_relaxed),
            .configured_speed =
                speed_from_bits(speed_bits_.load(std::memory_order_relaxed)),
            .paused = (flags_.load(std::memory_order_relaxed) & paused_flag) != 0U,
            .revision = revision_.load(std::memory_order_relaxed),
        };
        const auto real_now = read_real_qpc();

        const auto sequence_after = sequence_.load(std::memory_order_acquire);
        if (sequence_before == sequence_after && (sequence_after & 1U) == 0U) {
            return virtual_qpc_at(
                snapshot.real_anchor_qpc,
                snapshot.virtual_anchor_qpc,
                snapshot.configured_speed,
                snapshot.paused,
                real_now);
        }
    }
}

std::int64_t VirtualClock::read_real_qpc() const noexcept {
    if (!ready_) {
        return origins_.qpc;
    }

    return real_qpc_source_.read(real_qpc_source_.context);
}

UpdateResult VirtualClock::publish(const double speed, const bool paused) noexcept {
    sequence_.fetch_add(1, std::memory_order_acq_rel);

    const Snapshot previous{
        .real_anchor_qpc = real_anchor_qpc_.load(std::memory_order_relaxed),
        .virtual_anchor_qpc = virtual_anchor_qpc_.load(std::memory_order_relaxed),
        .configured_speed = speed_from_bits(speed_bits_.load(std::memory_order_relaxed)),
        .paused = (flags_.load(std::memory_order_relaxed) & paused_flag) != 0U,
        .revision = revision_.load(std::memory_order_relaxed),
    };

    const auto real_now = read_real_qpc();
    const auto virtual_now = virtual_qpc_at(
        previous.real_anchor_qpc,
        previous.virtual_anchor_qpc,
        previous.configured_speed,
        previous.paused,
        real_now);

    real_anchor_qpc_.store(real_now, std::memory_order_relaxed);
    virtual_anchor_qpc_.store(virtual_now, std::memory_order_relaxed);
    speed_bits_.store(speed_to_bits(speed), std::memory_order_relaxed);
    flags_.store(paused ? paused_flag : 0U, std::memory_order_relaxed);
    revision_.store(previous.revision + 1, std::memory_order_relaxed);

    sequence_.fetch_add(1, std::memory_order_release);
    return UpdateResult::applied;
}

}  // namespace timetransle::clock
