#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>

#include "timetransle/protocol/constants.hpp"

namespace timetransle::hook {

using UnityTimeGetter = float(__cdecl*)();
using UnityTimeSetter = void(__cdecl*)(float value);

struct UnityMonoDiscovery {
    bool mono_runtime_found{};
    bool unity_time_found{};
    void* get_time_scale{};
    void* set_time_scale{};
    void* get_delta_time{};
};

struct UnityTimeSnapshot {
    protocol::TargetRuntime target_runtime{protocol::TargetRuntime::generic};
    protocol::TimeBackend time_backend{protocol::TimeBackend::win32_clock};
    protocol::EffectState effect_state{protocol::EffectState::unverified};
    std::uint64_t applied_revision{};
    std::uint64_t applied_command_sequence{};
};

[[nodiscard]] UnityMonoDiscovery discover_unity_mono() noexcept;

class UnityMonoAdapter final {
public:
    UnityMonoAdapter() = default;

    UnityMonoAdapter(const UnityMonoAdapter&) = delete;
    UnityMonoAdapter& operator=(const UnityMonoAdapter&) = delete;

    void reset() noexcept;
    void mark_unity_detected_degraded() noexcept;

    [[nodiscard]] bool activate(
        UnityTimeGetter original_get_time_scale,
        UnityTimeSetter original_set_time_scale,
        UnityTimeGetter original_get_delta_time,
        std::uint64_t initial_revision) noexcept;

    void queue_external_state(
        double multiplier,
        bool paused,
        std::uint64_t revision,
        std::uint64_t command_sequence) noexcept;

    [[nodiscard]] bool uses_unity_backend() const noexcept;
    [[nodiscard]] UnityTimeSnapshot snapshot() noexcept;

    [[nodiscard]] float get_time_scale() noexcept;
    void set_time_scale(float value) noexcept;
    [[nodiscard]] float get_delta_time() noexcept;

private:
    [[nodiscard]] bool apply_locked() noexcept;
    [[nodiscard]] bool compute_effective_locked(float& output) noexcept;
    void fail_locked() noexcept;

    SRWLOCK lock_ = SRWLOCK_INIT;
    std::atomic<bool> unity_backend_active_{};
    UnityTimeGetter original_get_time_scale_{};
    UnityTimeSetter original_set_time_scale_{};
    UnityTimeGetter original_get_delta_time_{};
    bool game_scale_known_{};
    float game_scale_{1.0F};
    double desired_multiplier_{1.0};
    bool desired_paused_{};
    std::uint64_t desired_revision_{};
    std::uint64_t desired_command_sequence_{};
    std::uint64_t applied_revision_{};
    std::uint64_t applied_command_sequence_{};
    protocol::TargetRuntime target_runtime_{protocol::TargetRuntime::generic};
    protocol::TimeBackend time_backend_{protocol::TimeBackend::win32_clock};
    protocol::EffectState effect_state_{protocol::EffectState::unverified};
};

}  // namespace timetransle::hook
