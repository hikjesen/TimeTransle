#include "timetransle/hook/unity_mono_adapter.hpp"

#include <TlHelp32.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace timetransle::hook {
namespace {

struct MonoDomain;
struct MonoThread;
struct MonoAssembly;
struct MonoImage;
struct MonoClass;
struct MonoMethod;

using MonoGetRootDomain = MonoDomain*(__cdecl*)();
using MonoThreadAttach = MonoThread*(__cdecl*)(MonoDomain* domain);
using MonoThreadDetach = void(__cdecl*)(MonoThread* thread);
using MonoAssemblyForeachCallback = void(__cdecl*)(MonoAssembly* assembly, void* data);
using MonoAssemblyForeach = void(__cdecl*)(MonoAssemblyForeachCallback callback, void* data);
using MonoAssemblyGetImage = MonoImage*(__cdecl*)(MonoAssembly* assembly);
using MonoClassFromName = MonoClass*(__cdecl*)(
    MonoImage* image,
    const char* name_space,
    const char* name);
using MonoClassGetMethodFromName = MonoMethod*(__cdecl*)(
    MonoClass* klass,
    const char* name,
    int parameter_count);
using MonoLookupInternalCall = void*(__cdecl*)(MonoMethod* method);

struct MonoApi {
    MonoGetRootDomain get_root_domain{};
    MonoThreadAttach thread_attach{};
    MonoThreadDetach thread_detach{};
    MonoAssemblyForeach assembly_foreach{};
    MonoAssemblyGetImage assembly_get_image{};
    MonoClassFromName class_from_name{};
    MonoClassGetMethodFromName class_get_method_from_name{};
    MonoLookupInternalCall lookup_internal_call{};
};

class UniqueSnapshot final {
public:
    explicit UniqueSnapshot(const HANDLE value) noexcept : value_(value) {}
    ~UniqueSnapshot() {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }

    UniqueSnapshot(const UniqueSnapshot&) = delete;
    UniqueSnapshot& operator=(const UniqueSnapshot&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_{};
};

class ExclusiveLock final {
public:
    explicit ExclusiveLock(SRWLOCK& lock) noexcept : lock_(lock) {
        AcquireSRWLockExclusive(&lock_);
    }
    ~ExclusiveLock() { ReleaseSRWLockExclusive(&lock_); }

    ExclusiveLock(const ExclusiveLock&) = delete;
    ExclusiveLock& operator=(const ExclusiveLock&) = delete;

private:
    SRWLOCK& lock_;
};

template <typename Function>
[[nodiscard]] Function resolve(const HMODULE module, const char* const name) noexcept {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] bool resolve_mono_api(const HMODULE module, MonoApi& output) noexcept {
    output = {
        .get_root_domain = resolve<MonoGetRootDomain>(module, "mono_get_root_domain"),
        .thread_attach = resolve<MonoThreadAttach>(module, "mono_thread_attach"),
        .thread_detach = resolve<MonoThreadDetach>(module, "mono_thread_detach"),
        .assembly_foreach = resolve<MonoAssemblyForeach>(module, "mono_assembly_foreach"),
        .assembly_get_image = resolve<MonoAssemblyGetImage>(module, "mono_assembly_get_image"),
        .class_from_name = resolve<MonoClassFromName>(module, "mono_class_from_name"),
        .class_get_method_from_name = resolve<MonoClassGetMethodFromName>(
            module,
            "mono_class_get_method_from_name"),
        .lookup_internal_call = resolve<MonoLookupInternalCall>(
            module,
            "mono_lookup_internal_call"),
    };
    return output.get_root_domain != nullptr && output.thread_attach != nullptr &&
           output.thread_detach != nullptr &&
           output.assembly_foreach != nullptr && output.assembly_get_image != nullptr &&
           output.class_from_name != nullptr &&
           output.class_get_method_from_name != nullptr &&
           output.lookup_internal_call != nullptr;
}

struct AssemblySearchContext {
    const MonoApi* api{};
    UnityMonoDiscovery* result{};
};

void __cdecl inspect_assembly(MonoAssembly* const assembly, void* const raw_context) {
    auto& context = *static_cast<AssemblySearchContext*>(raw_context);
    if (context.result->unity_time_found || assembly == nullptr) {
        return;
    }

    auto* const image = context.api->assembly_get_image(assembly);
    auto* const klass = image == nullptr
                            ? nullptr
                            : context.api->class_from_name(image, "UnityEngine", "Time");
    if (klass == nullptr) {
        return;
    }

    auto* const get_time_scale =
        context.api->class_get_method_from_name(klass, "get_timeScale", 0);
    auto* const set_time_scale =
        context.api->class_get_method_from_name(klass, "set_timeScale", 1);
    auto* const get_delta_time =
        context.api->class_get_method_from_name(klass, "get_deltaTime", 0);
    if (get_time_scale == nullptr || set_time_scale == nullptr ||
        get_delta_time == nullptr) {
        return;
    }

    const auto get_target = context.api->lookup_internal_call(get_time_scale);
    const auto set_target = context.api->lookup_internal_call(set_time_scale);
    const auto delta_target = context.api->lookup_internal_call(get_delta_time);
    if (get_target == nullptr || set_target == nullptr || delta_target == nullptr ||
        get_target == set_target || get_target == delta_target ||
        set_target == delta_target) {
        return;
    }

    context.result->unity_time_found = true;
    context.result->get_time_scale = get_target;
    context.result->set_time_scale = set_target;
    context.result->get_delta_time = delta_target;
}

[[nodiscard]] bool nearly_equal(const float left, const float right) noexcept {
    const auto scale = std::max({1.0F, std::fabs(left), std::fabs(right)});
    return std::fabs(left - right) <= scale * 0.0001F;
}

}  // namespace

UnityMonoDiscovery discover_unity_mono() noexcept {
    UnityMonoDiscovery result;
    UniqueSnapshot modules{CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        GetCurrentProcessId())};
    if (modules.get() == INVALID_HANDLE_VALUE) {
        return result;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(modules.get(), &entry) == FALSE) {
        return result;
    }

    MonoApi api;
    do {
        if (resolve_mono_api(entry.hModule, api)) {
            result.mono_runtime_found = true;
            break;
        }
    } while (Module32NextW(modules.get(), &entry) != FALSE);

    if (!result.mono_runtime_found) {
        return result;
    }

    auto* const domain = api.get_root_domain();
    if (domain == nullptr) {
        return result;
    }

    // Initialization runs on an injector-created native thread. It is not yet
    // registered with Mono, and mono_thread_current is not safe to call from
    // that state on Unity's cooperative runtime. Attach first, before invoking
    // any API that requires Mono thread state.
    auto* const attached_thread = api.thread_attach(domain);
    if (attached_thread == nullptr) {
        return result;
    }

    AssemblySearchContext search{.api = &api, .result = &result};
    api.assembly_foreach(&inspect_assembly, &search);
    api.thread_detach(attached_thread);
    return result;
}

void UnityMonoAdapter::reset() noexcept {
    ExclusiveLock lock{lock_};
    unity_backend_active_.store(false, std::memory_order_release);
    original_get_time_scale_ = nullptr;
    original_set_time_scale_ = nullptr;
    original_get_delta_time_ = nullptr;
    game_scale_known_ = false;
    game_scale_ = 1.0F;
    desired_multiplier_ = 1.0;
    desired_paused_ = false;
    desired_revision_ = 0;
    desired_command_sequence_ = 0;
    applied_revision_ = 0;
    applied_command_sequence_ = 0;
    target_runtime_ = protocol::TargetRuntime::generic;
    time_backend_ = protocol::TimeBackend::win32_clock;
    effect_state_ = protocol::EffectState::unverified;
}

void UnityMonoAdapter::mark_unity_detected_degraded() noexcept {
    ExclusiveLock lock{lock_};
    unity_backend_active_.store(false, std::memory_order_release);
    target_runtime_ = protocol::TargetRuntime::unity_mono;
    time_backend_ = protocol::TimeBackend::win32_clock;
    effect_state_ = protocol::EffectState::degraded;
}

bool UnityMonoAdapter::activate(
    const UnityTimeGetter original_get_time_scale,
    const UnityTimeSetter original_set_time_scale,
    const UnityTimeGetter original_get_delta_time,
    const std::uint64_t initial_revision) noexcept {
    if (original_get_time_scale == nullptr || original_set_time_scale == nullptr ||
        original_get_delta_time == nullptr) {
        return false;
    }

    ExclusiveLock lock{lock_};
    original_get_time_scale_ = original_get_time_scale;
    original_set_time_scale_ = original_set_time_scale;
    original_get_delta_time_ = original_get_delta_time;
    desired_revision_ = initial_revision;
    applied_revision_ = initial_revision;
    target_runtime_ = protocol::TargetRuntime::unity_mono;
    time_backend_ = protocol::TimeBackend::unity_mono;
    effect_state_ = protocol::EffectState::applied;
    unity_backend_active_.store(true, std::memory_order_release);
    return true;
}

void UnityMonoAdapter::queue_external_state(
    const double multiplier,
    const bool paused,
    const std::uint64_t revision,
    const std::uint64_t command_sequence) noexcept {
    ExclusiveLock lock{lock_};
    if (!unity_backend_active_.load(std::memory_order_acquire)) {
        return;
    }
    desired_multiplier_ = multiplier;
    desired_paused_ = paused;
    desired_revision_ = revision;
    desired_command_sequence_ = command_sequence;
    effect_state_ = protocol::EffectState::pending;
}

bool UnityMonoAdapter::uses_unity_backend() const noexcept {
    return unity_backend_active_.load(std::memory_order_acquire);
}

UnityTimeSnapshot UnityMonoAdapter::snapshot() noexcept {
    ExclusiveLock lock{lock_};
    return {
        .target_runtime = target_runtime_,
        .time_backend = time_backend_,
        .effect_state = effect_state_,
        .applied_revision = applied_revision_,
        .applied_command_sequence = applied_command_sequence_,
    };
}

float UnityMonoAdapter::get_time_scale() noexcept {
    ExclusiveLock lock{lock_};
    if (!unity_backend_active_.load(std::memory_order_acquire) ||
        original_get_time_scale_ == nullptr) {
        return original_get_time_scale_ == nullptr ? 1.0F : original_get_time_scale_();
    }

    const auto actual = original_get_time_scale_();
    if (!game_scale_known_) {
        if (!std::isfinite(actual) || actual < 0.0F) {
            fail_locked();
            return actual;
        }
        game_scale_ = actual;
        game_scale_known_ = true;
    }
    return game_scale_;
}

void UnityMonoAdapter::set_time_scale(const float value) noexcept {
    ExclusiveLock lock{lock_};
    if (!unity_backend_active_.load(std::memory_order_acquire) ||
        original_set_time_scale_ == nullptr) {
        if (original_set_time_scale_ != nullptr) {
            original_set_time_scale_(value);
        }
        return;
    }

    if (!std::isfinite(value) || value < 0.0F) {
        fail_locked();
        return;
    }

    game_scale_ = value;
    game_scale_known_ = true;
    if (!apply_locked()) {
        original_set_time_scale_(value);
    }
}

float UnityMonoAdapter::get_delta_time() noexcept {
    UnityTimeGetter original{};
    {
        ExclusiveLock lock{lock_};
        if (unity_backend_active_.load(std::memory_order_acquire) &&
            desired_revision_ != applied_revision_) {
            static_cast<void>(apply_locked());
        }
        original = original_get_delta_time_;
    }
    return original == nullptr ? 0.0F : original();
}

bool UnityMonoAdapter::apply_locked() noexcept {
    if (original_get_time_scale_ == nullptr || original_set_time_scale_ == nullptr) {
        fail_locked();
        return false;
    }

    if (!game_scale_known_) {
        const auto actual = original_get_time_scale_();
        if (!std::isfinite(actual) || actual < 0.0F) {
            fail_locked();
            return false;
        }
        game_scale_ = actual;
        game_scale_known_ = true;
    }

    float effective{};
    if (!compute_effective_locked(effective)) {
        original_set_time_scale_(game_scale_);
        fail_locked();
        return false;
    }

    original_set_time_scale_(effective);
    const auto actual = original_get_time_scale_();
    if (!std::isfinite(actual) || !nearly_equal(actual, effective)) {
        fail_locked();
        return false;
    }

    applied_revision_ = desired_revision_;
    applied_command_sequence_ = desired_command_sequence_;
    effect_state_ = protocol::EffectState::applied;
    return true;
}

bool UnityMonoAdapter::compute_effective_locked(float& output) noexcept {
    if (!std::isfinite(desired_multiplier_) || desired_multiplier_ <= 0.0 ||
        !std::isfinite(game_scale_) || game_scale_ < 0.0F) {
        return false;
    }
    if (desired_paused_) {
        output = 0.0F;
        return true;
    }

    const auto effective = static_cast<double>(game_scale_) * desired_multiplier_;
    if (!std::isfinite(effective) ||
        effective > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    output = static_cast<float>(effective);
    return std::isfinite(output);
}

void UnityMonoAdapter::fail_locked() noexcept {
    effect_state_ = protocol::EffectState::failed;
}

}  // namespace timetransle::hook
