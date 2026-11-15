#include "timetransle/hook/hook_installer.hpp"

namespace timetransle::hook {

HookInstaller::HookInstaller(const HookBackend backend) noexcept : backend_(backend) {}

InstallResult HookInstaller::install(
    const std::span<const HookSpec> hooks,
    const HooksCreatedCallback hooks_created,
    void* const callback_context) noexcept {
    if (installed_) {
        return InstallResult::already_installed;
    }
    if (!backend_is_valid()) {
        return InstallResult::invalid_backend;
    }
    if (hooks.empty() || hooks_created == nullptr) {
        return InstallResult::invalid_spec;
    }
    if (hooks.size() > created_targets_.size()) {
        return InstallResult::too_many_hooks;
    }

    for (std::size_t index = 0; index < hooks.size(); ++index) {
        const auto& hook = hooks[index];
        if (hook.target == nullptr || hook.detour == nullptr || hook.original == nullptr) {
            return InstallResult::invalid_spec;
        }

        for (std::size_t previous = 0; previous < index; ++previous) {
            if (hooks[previous].target == hook.target) {
                return InstallResult::duplicate_target;
            }
        }
    }

    if (!backend_.initialize(backend_.context)) {
        return InstallResult::initialize_failed;
    }
    backend_initialized_ = true;

    for (const auto& hook : hooks) {
        if (!backend_.create(
                backend_.context,
                hook.target,
                hook.detour,
                hook.original)) {
            rollback();
            return InstallResult::create_failed;
        }

        created_targets_[created_count_] = hook.target;
        ++created_count_;
    }

    if (!hooks_created(callback_context)) {
        rollback();
        return InstallResult::prepare_failed;
    }

    for (const auto& hook : hooks) {
        if (!backend_.queue_enable(backend_.context, hook.target)) {
            rollback();
            return InstallResult::queue_failed;
        }
    }

    if (!backend_.apply_queued(backend_.context)) {
        rollback();
        return InstallResult::apply_failed;
    }

    installed_ = true;
    return InstallResult::installed;
}

bool HookInstaller::is_installed() const noexcept {
    return installed_;
}

bool HookInstaller::backend_is_valid() const noexcept {
    return backend_.initialize != nullptr && backend_.create != nullptr &&
           backend_.queue_enable != nullptr && backend_.apply_queued != nullptr &&
           backend_.disable_all != nullptr && backend_.remove != nullptr &&
           backend_.uninitialize != nullptr;
}

void HookInstaller::rollback() noexcept {
    if (!backend_initialized_) {
        return;
    }

    backend_.disable_all(backend_.context);
    while (created_count_ != 0) {
        --created_count_;
        backend_.remove(backend_.context, created_targets_[created_count_]);
        created_targets_[created_count_] = nullptr;
    }
    backend_.uninitialize(backend_.context);
    backend_initialized_ = false;
}

}  // namespace timetransle::hook
