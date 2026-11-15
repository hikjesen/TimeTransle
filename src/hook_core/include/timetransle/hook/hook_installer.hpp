#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace timetransle::hook {

inline constexpr std::size_t maximum_hook_count = 8;

struct HookBackend {
    void* context{};
    bool (*initialize)(void* context) noexcept {};
    bool (*create)(
        void* context,
        void* target,
        void* detour,
        void** original) noexcept {};
    bool (*queue_enable)(void* context, void* target) noexcept {};
    bool (*apply_queued)(void* context) noexcept {};
    void (*disable_all)(void* context) noexcept {};
    void (*remove)(void* context, void* target) noexcept {};
    void (*uninitialize)(void* context) noexcept {};
};

struct HookSpec {
    void* target{};
    void* detour{};
    void** original{};
};

using HooksCreatedCallback = bool (*)(void* context) noexcept;

enum class InstallResult {
    installed,
    invalid_backend,
    invalid_spec,
    too_many_hooks,
    duplicate_target,
    already_installed,
    initialize_failed,
    create_failed,
    prepare_failed,
    queue_failed,
    apply_failed,
};

class HookInstaller final {
public:
    explicit HookInstaller(HookBackend backend) noexcept;

    HookInstaller(const HookInstaller&) = delete;
    HookInstaller& operator=(const HookInstaller&) = delete;
    HookInstaller(HookInstaller&&) = delete;
    HookInstaller& operator=(HookInstaller&&) = delete;

    [[nodiscard]] InstallResult install(
        std::span<const HookSpec> hooks,
        HooksCreatedCallback hooks_created,
        void* callback_context) noexcept;

    [[nodiscard]] bool is_installed() const noexcept;

private:
    [[nodiscard]] bool backend_is_valid() const noexcept;
    void rollback() noexcept;

    HookBackend backend_;
    std::array<void*, maximum_hook_count> created_targets_{};
    std::size_t created_count_{};
    bool backend_initialized_{};
    bool installed_{};
};

}  // namespace timetransle::hook
