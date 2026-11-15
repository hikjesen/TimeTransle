#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace {

struct MonoDomain {};
struct MonoThread {};
struct MonoAssembly {};
struct MonoImage {};
struct MonoClass {};
struct MonoMethod {
    std::uint32_t id{};
};

using AssemblyCallback = void(__cdecl*)(MonoAssembly* assembly, void* data);

MonoDomain domain;
MonoThread thread;
MonoAssembly assembly;
MonoImage image;
MonoClass unity_time;
MonoMethod get_time_scale{1};
MonoMethod set_time_scale{2};
MonoMethod get_delta_time{3};

thread_local bool thread_attached{};
std::atomic<std::uint32_t> thread_current_calls{};
std::atomic<std::uint32_t> thread_attach_calls{};
std::atomic<std::uint32_t> thread_detach_calls{};
std::atomic<std::uint32_t> assembly_foreach_calls{};
std::atomic<std::uint32_t> order_violations{};

float __cdecl fake_get_time_scale() {
    return 1.0F;
}

void __cdecl fake_set_time_scale(float) {}

float __cdecl fake_get_delta_time() {
    return 0.016F;
}

}  // namespace

extern "C" {

__declspec(dllexport) MonoDomain* __cdecl mono_get_root_domain() {
    return &domain;
}

__declspec(dllexport) MonoThread* __cdecl mono_thread_current() {
    thread_current_calls.fetch_add(1, std::memory_order_relaxed);
    return thread_attached ? &thread : nullptr;
}

__declspec(dllexport) MonoThread* __cdecl mono_thread_attach(MonoDomain* value) {
    thread_attach_calls.fetch_add(1, std::memory_order_relaxed);
    if (value != &domain) {
        order_violations.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    thread_attached = true;
    return &thread;
}

__declspec(dllexport) void __cdecl mono_thread_detach(MonoThread* value) {
    thread_detach_calls.fetch_add(1, std::memory_order_relaxed);
    if (!thread_attached || value != &thread) {
        order_violations.fetch_add(1, std::memory_order_relaxed);
    }
    thread_attached = false;
}

__declspec(dllexport) void __cdecl mono_assembly_foreach(
    AssemblyCallback callback,
    void* data) {
    assembly_foreach_calls.fetch_add(1, std::memory_order_relaxed);
    if (!thread_attached || callback == nullptr) {
        order_violations.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    callback(&assembly, data);
}

__declspec(dllexport) MonoImage* __cdecl mono_assembly_get_image(
    MonoAssembly* value) {
    return value == &assembly ? &image : nullptr;
}

__declspec(dllexport) MonoClass* __cdecl mono_class_from_name(
    MonoImage* value,
    const char* name_space,
    const char* name) {
    return value == &image && name_space != nullptr && name != nullptr &&
                   std::strcmp(name_space, "UnityEngine") == 0 &&
                   std::strcmp(name, "Time") == 0
               ? &unity_time
               : nullptr;
}

__declspec(dllexport) MonoMethod* __cdecl mono_class_get_method_from_name(
    MonoClass* value,
    const char* name,
    int parameter_count) {
    if (value != &unity_time || name == nullptr) {
        return nullptr;
    }
    if (parameter_count == 0 && std::strcmp(name, "get_timeScale") == 0) {
        return &get_time_scale;
    }
    if (parameter_count == 1 && std::strcmp(name, "set_timeScale") == 0) {
        return &set_time_scale;
    }
    if (parameter_count == 0 && std::strcmp(name, "get_deltaTime") == 0) {
        return &get_delta_time;
    }
    return nullptr;
}

__declspec(dllexport) void* __cdecl mono_lookup_internal_call(MonoMethod* method) {
    if (method == &get_time_scale) {
        return reinterpret_cast<void*>(&fake_get_time_scale);
    }
    if (method == &set_time_scale) {
        return reinterpret_cast<void*>(&fake_set_time_scale);
    }
    if (method == &get_delta_time) {
        return reinterpret_cast<void*>(&fake_get_delta_time);
    }
    return nullptr;
}

__declspec(dllexport) void __cdecl TimeTransleFakeMonoReset() {
    thread_attached = false;
    thread_current_calls.store(0, std::memory_order_relaxed);
    thread_attach_calls.store(0, std::memory_order_relaxed);
    thread_detach_calls.store(0, std::memory_order_relaxed);
    assembly_foreach_calls.store(0, std::memory_order_relaxed);
    order_violations.store(0, std::memory_order_relaxed);
}

__declspec(dllexport) std::uint32_t __cdecl
TimeTransleFakeMonoThreadCurrentCalls() {
    return thread_current_calls.load(std::memory_order_relaxed);
}

__declspec(dllexport) std::uint32_t __cdecl
TimeTransleFakeMonoThreadAttachCalls() {
    return thread_attach_calls.load(std::memory_order_relaxed);
}

__declspec(dllexport) std::uint32_t __cdecl
TimeTransleFakeMonoThreadDetachCalls() {
    return thread_detach_calls.load(std::memory_order_relaxed);
}

__declspec(dllexport) std::uint32_t __cdecl
TimeTransleFakeMonoAssemblyForeachCalls() {
    return assembly_foreach_calls.load(std::memory_order_relaxed);
}

__declspec(dllexport) std::uint32_t __cdecl
TimeTransleFakeMonoOrderViolations() {
    return order_violations.load(std::memory_order_relaxed);
}

}  // extern "C"
