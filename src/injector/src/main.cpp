#include <Windows.h>
#include <TlHelp32.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "timetransle/common/build_info.hpp"
#include "timetransle/common/diagnostics.hpp"
#include "timetransle/hook/api.hpp"
#include "timetransle/ipc/bootstrap.hpp"
#include "timetransle/protocol/error_code.hpp"
#include "timetransle/safety/windows_inspector.hpp"

namespace {

using timetransle::hook::GetBuildIdFunction;
using timetransle::hook::GetApiVersionFunction;
using timetransle::hook::HookApiResult;
using timetransle::hook::InitializeRequestV1;
using timetransle::protocol::ErrorCode;
using timetransle::safety::MachineType;

class UniqueHandle final {
public:
    explicit UniqueHandle(const HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~UniqueHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

private:
    HANDLE handle_{};
};

class LocalModule final {
public:
    explicit LocalModule(const HMODULE module = nullptr) noexcept : module_(module) {}
    ~LocalModule() {
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    LocalModule(const LocalModule&) = delete;
    LocalModule& operator=(const LocalModule&) = delete;

    LocalModule(LocalModule&& other) noexcept : module_(other.module_) {
        other.module_ = nullptr;
    }

    LocalModule& operator=(LocalModule&& other) noexcept {
        if (this != &other) {
            if (module_ != nullptr) {
                FreeLibrary(module_);
            }
            module_ = other.module_;
            other.module_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HMODULE get() const noexcept {
        return module_;
    }

private:
    HMODULE module_{};
};

class RemoteAllocation final {
public:
    RemoteAllocation(
        const HANDLE process,
        const std::size_t bytes) noexcept
        : process_(process),
          address_(VirtualAllocEx(
              process,
              nullptr,
              bytes,
              MEM_COMMIT | MEM_RESERVE,
              PAGE_READWRITE)) {}

    ~RemoteAllocation() {
        if (address_ != nullptr) {
            VirtualFreeEx(process_, address_, 0, MEM_RELEASE);
        }
    }

    RemoteAllocation(const RemoteAllocation&) = delete;
    RemoteAllocation& operator=(const RemoteAllocation&) = delete;

    [[nodiscard]] void* get() const noexcept {
        return address_;
    }

private:
    HANDLE process_{};
    void* address_{};
};

[[nodiscard]] constexpr MachineType injector_machine() noexcept {
    return sizeof(void*) == 8 ? MachineType::x64 : MachineType::x86;
}

[[nodiscard]] std::wstring lowercase(std::wstring value) {
    for (auto& character : value) {
        character = static_cast<wchar_t>(std::towlower(character));
    }
    return value;
}

[[nodiscard]] std::filesystem::path executable_path() {
    std::array<wchar_t, 32'768> buffer{};
    const auto length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) {
        return {};
    }
    return std::filesystem::path{std::wstring_view{buffer.data(), length}};
}

[[nodiscard]] bool read_pe_machine(
    const std::filesystem::path& path,
    WORD& machine) noexcept {
    UniqueHandle file{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER file_size{};
    if (GetFileSizeEx(file.get(), &file_size) == FALSE ||
        file_size.QuadPart <
            static_cast<LONGLONG>(sizeof(IMAGE_DOS_HEADER) + sizeof(DWORD) +
                                  sizeof(IMAGE_FILE_HEADER))) {
        return false;
    }

    IMAGE_DOS_HEADER dos_header{};
    DWORD bytes_read{};
    if (ReadFile(
            file.get(),
            &dos_header,
            sizeof(dos_header),
            &bytes_read,
            nullptr) == FALSE ||
        bytes_read != sizeof(dos_header) ||
        dos_header.e_magic != IMAGE_DOS_SIGNATURE ||
        dos_header.e_lfanew <= 0) {
        return false;
    }

    const auto nt_offset = static_cast<LONGLONG>(dos_header.e_lfanew);
    if (nt_offset + static_cast<LONGLONG>(
                        sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER)) >
        file_size.QuadPart) {
        return false;
    }

    LARGE_INTEGER position{.QuadPart = nt_offset};
    if (SetFilePointerEx(file.get(), position, nullptr, FILE_BEGIN) == FALSE) {
        return false;
    }

    DWORD signature{};
    IMAGE_FILE_HEADER file_header{};
    if (ReadFile(
            file.get(),
            &signature,
            sizeof(signature),
            &bytes_read,
            nullptr) == FALSE ||
        bytes_read != sizeof(signature) ||
        signature != IMAGE_NT_SIGNATURE ||
        ReadFile(
            file.get(),
            &file_header,
            sizeof(file_header),
            &bytes_read,
            nullptr) == FALSE ||
        bytes_read != sizeof(file_header)) {
        return false;
    }

    machine = file_header.Machine;
    return true;
}

[[nodiscard]] bool parse_u32(
    const std::wstring_view text,
    std::uint32_t& output) noexcept {
    wchar_t* end{};
    const auto value = wcstoul(text.data(), &end, 10);
    if (end != text.data() + text.size() || value > UINT32_MAX) {
        return false;
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool parse_u64_hex(
    const std::wstring_view text,
    std::uint64_t& output) noexcept {
    wchar_t* end{};
    const auto value = _wcstoui64(text.data(), &end, 16);
    if (end != text.data() + text.size()) {
        return false;
    }
    output = value;
    return true;
}

[[nodiscard]] bool read_exact(
    const HANDLE source,
    const std::span<std::byte> output) noexcept {
    std::size_t completed = 0;
    while (completed < output.size()) {
        DWORD bytes_read{};
        const auto remaining = output.size() - completed;
        if (remaining > MAXDWORD ||
            ReadFile(
                source,
                output.data() + completed,
                static_cast<DWORD>(remaining),
                &bytes_read,
                nullptr) == FALSE ||
            bytes_read == 0) {
            return false;
        }
        completed += bytes_read;
    }
    return true;
}

[[nodiscard]] constexpr std::uint64_t file_time_to_uint64(
    const FILETIME value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           value.dwLowDateTime;
}

[[nodiscard]] bool creation_time_matches(
    HANDLE process,
    std::uint64_t expected) noexcept;

[[nodiscard]] bool process_identity_matches(
    const std::uint32_t process_id,
    const std::uint64_t creation_time) noexcept {
    UniqueHandle process{OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        process_id)};
    return process.get() != nullptr &&
           creation_time_matches(process.get(), creation_time);
}

[[nodiscard]] bool bootstrap_is_valid(
    const timetransle::ipc::InjectorBootstrapV1& bootstrap,
    const std::uint32_t target_process_id,
    const std::uint64_t target_creation_time) noexcept {
    const auto terminator = std::find(
        bootstrap.pipe_name.begin(),
        bootstrap.pipe_name.end(),
        L'\0');
    constexpr std::wstring_view pipe_prefix{LR"(\\.\pipe\TimeTransle.)"};
    const auto session_has_entropy = std::any_of(
        bootstrap.session_id.begin(),
        bootstrap.session_id.end(),
        [](const std::byte value) { return value != std::byte{0}; });
    const auto token_has_entropy = std::any_of(
        bootstrap.session_token.begin(),
        bootstrap.session_token.end(),
        [](const std::byte value) { return value != std::byte{0}; });

    return bootstrap.struct_size ==
               sizeof(timetransle::ipc::InjectorBootstrapV1) &&
           bootstrap.magic ==
               timetransle::ipc::injector_bootstrap_magic &&
           bootstrap.version ==
               timetransle::ipc::injector_bootstrap_version &&
           (bootstrap.flags == timetransle::ipc::injector_bootstrap_enable_ipc ||
            bootstrap.flags == (timetransle::ipc::injector_bootstrap_enable_ipc | timetransle::ipc::injector_bootstrap_checkpoint)) &&
           bootstrap.target_process_id == target_process_id &&
           bootstrap.target_creation_time == target_creation_time &&
           bootstrap.controller_process_id != 0 &&
           bootstrap.controller_creation_time != 0 &&
           terminator != bootstrap.pipe_name.begin() &&
           terminator != bootstrap.pipe_name.end() &&
           std::wstring_view{
               bootstrap.pipe_name.data(),
               static_cast<std::size_t>(
                   terminator - bootstrap.pipe_name.begin())}
               .starts_with(pipe_prefix) &&
           session_has_entropy &&
           token_has_entropy &&
           process_identity_matches(
               bootstrap.controller_process_id,
               bootstrap.controller_creation_time);
}

[[nodiscard]] std::optional<InitializeRequestV1>
read_controller_bootstrap(
    const std::wstring_view handle_text,
    const std::uint32_t target_process_id,
    const std::uint64_t target_creation_time) noexcept {
    std::uint64_t raw_handle{};
    if (!parse_u64_hex(handle_text, raw_handle) ||
        raw_handle == 0 ||
        raw_handle >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uintptr_t>::max())) {
        return std::nullopt;
    }

    UniqueHandle source{reinterpret_cast<HANDLE>(
        static_cast<std::uintptr_t>(raw_handle))};
    timetransle::ipc::InjectorBootstrapV1 bootstrap;
    if (!read_exact(
            source.get(),
            std::as_writable_bytes(std::span{&bootstrap, 1})) ||
        !bootstrap_is_valid(
            bootstrap,
            target_process_id,
            target_creation_time)) {
        return std::nullopt;
    }

    InitializeRequestV1 request;
    request.process_id = target_process_id;
    request.flags =
        timetransle::hook::product_bootstrap_flag |
        timetransle::hook::enable_ipc_flag;
    if ((bootstrap.flags & timetransle::ipc::injector_bootstrap_checkpoint) != 0U)
        request.flags |= timetransle::hook::enable_checkpoint_flag;
    request.controller_process_id = bootstrap.controller_process_id;
    request.controller_creation_time =
        bootstrap.controller_creation_time;
    request.session_id = bootstrap.session_id;
    request.session_token = bootstrap.session_token;
    std::copy(
        bootstrap.pipe_name.begin(),
        bootstrap.pipe_name.end(),
        request.pipe_name.begin());
    return request;
}

[[nodiscard]] bool creation_time_matches(
    const HANDLE process,
    const std::uint64_t expected) noexcept {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    return GetProcessTimes(
               process,
               &creation,
               &exit,
               &kernel,
               &user) != FALSE &&
           file_time_to_uint64(creation) == expected;
}

[[nodiscard]] std::optional<std::uintptr_t> remote_module_base(
    const std::uint32_t process_id,
    const std::wstring_view module_name) {
    UniqueHandle snapshot{CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        process_id)};
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    MODULEENTRY32W module{.dwSize = sizeof(MODULEENTRY32W)};
    if (Module32FirstW(snapshot.get(), &module) == FALSE) {
        return std::nullopt;
    }
    const auto expected = lowercase(std::wstring{module_name});
    do {
        if (lowercase(module.szModule) == expected) {
            return reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
        }
    } while (Module32NextW(snapshot.get(), &module) != FALSE);
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uintptr_t> remote_function_address(
    const std::uint32_t process_id,
    const void* const local_function) {
    HMODULE local_owner{};
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(local_function),
            &local_owner) == FALSE) {
        return std::nullopt;
    }

    std::array<wchar_t, MAX_PATH> module_path{};
    const auto length = GetModuleFileNameW(
        local_owner,
        module_path.data(),
        static_cast<DWORD>(module_path.size()));
    if (length == 0 || length == module_path.size()) {
        return std::nullopt;
    }

    const auto module_name =
        std::filesystem::path{std::wstring_view{module_path.data(), length}}
            .filename()
            .wstring();
    const auto remote_base = remote_module_base(process_id, module_name);
    if (!remote_base.has_value()) {
        return std::nullopt;
    }

    const auto offset =
        reinterpret_cast<std::uintptr_t>(local_function) -
        reinterpret_cast<std::uintptr_t>(local_owner);
    return *remote_base + offset;
}

[[nodiscard]] ErrorCode wait_for_remote_thread(
    const HANDLE thread,
    DWORD& exit_code) noexcept {
    if (WaitForSingleObject(thread, 10'000) != WAIT_OBJECT_0 ||
        GetExitCodeThread(thread, &exit_code) == FALSE) {
        return ErrorCode::timeout;
    }
    return ErrorCode::ok;
}

[[nodiscard]] bool write_remote(
    const HANDLE process,
    void* const destination,
    const void* const source,
    const std::size_t bytes) noexcept {
    SIZE_T written{};
    return WriteProcessMemory(
               process,
               destination,
               source,
               bytes,
               &written) != FALSE &&
           written == bytes;
}

struct TrustedHook {
    std::filesystem::path path;
    LocalModule module;
    std::uintptr_t initialize_offset{};
    bool trusted{};
};

[[nodiscard]] TrustedHook inspect_hook_binary() {
    TrustedHook result;
    const auto injector_path = executable_path();
    if (injector_path.empty()) {
        return result;
    }

    const auto hook_filename =
        sizeof(void*) == 8 ? L"TimeTransleHook64.dll" : L"TimeTransleHook32.dll";
    result.path = injector_path.parent_path() / hook_filename;

    WORD machine{};
    const auto expected_machine =
        sizeof(void*) == 8 ? IMAGE_FILE_MACHINE_AMD64 : IMAGE_FILE_MACHINE_I386;
    if (!read_pe_machine(result.path, machine) ||
        machine != expected_machine) {
        return result;
    }

    const auto module = LoadLibraryExW(
        result.path.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        return result;
    }
    result.module = LocalModule{module};

    const auto build_id_function = reinterpret_cast<GetBuildIdFunction>(
        GetProcAddress(module, "TimeTransleGetBuildId"));
    const auto api_version_function = reinterpret_cast<GetApiVersionFunction>(
        GetProcAddress(module, "TimeTransleGetApiVersion"));
    const auto initialize =
        GetProcAddress(module, "TimeTransleInitializeThread");
    if (build_id_function == nullptr ||
        api_version_function == nullptr ||
        initialize == nullptr) {
        return result;
    }

    std::array<char, 128> build_id{};
    if (build_id_function(
            build_id.data(),
            static_cast<std::uint32_t>(build_id.size())) != HookApiResult::ok ||
        std::string_view{build_id.data()} != timetransle::build_info::build_id ||
        api_version_function() !=
            timetransle::hook::local_bootstrap_version) {
        return result;
    }

    result.initialize_offset =
        reinterpret_cast<std::uintptr_t>(initialize) -
        reinterpret_cast<std::uintptr_t>(module);
    result.trusted = true;
    return result;
}

[[nodiscard]] ErrorCode inject(
    const std::uint32_t process_id,
    const std::uint64_t creation_time,
    const InitializeRequestV1& initialize_request) {
    auto hook = inspect_hook_binary();
    const auto inspection = timetransle::safety::inspect_target(
        process_id,
        creation_time,
        injector_machine(),
        hook.trusted);
    if (!inspection.decision.allowed) {
        return inspection.decision.error;
    }

    UniqueHandle process{OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
        FALSE,
        process_id)};
    if (process.get() == nullptr) {
        return ErrorCode::access_denied;
    }
    if (!creation_time_matches(process.get(), creation_time)) {
        return ErrorCode::target_identity_changed;
    }

    // A CREATE_SUSPENDED process has ntdll mapped but no initialized loader module list.
    // Let a native thread run the Windows loader and exit; the application entry thread
    // remains suspended throughout. Ordinary post-attach injection keeps the existing path.
    if ((initialize_request.flags & timetransle::hook::enable_checkpoint_flag) != 0U) {
        std::uintptr_t native_base = 0;
        std::uintptr_t cursor = 0;
        MEMORY_BASIC_INFORMATION region{};
        while (VirtualQueryEx(process.get(), reinterpret_cast<void*>(cursor), &region, sizeof(region))) {
            if (region.Type == MEM_IMAGE && region.BaseAddress == region.AllocationBase) {
                std::array<wchar_t, MAX_PATH> mapped{};
                const auto length = K32GetMappedFileNameW(process.get(), region.AllocationBase,
                    mapped.data(), static_cast<DWORD>(mapped.size()));
                if (length && lowercase(std::filesystem::path(std::wstring_view(mapped.data(), length)).filename().wstring()) == L"ntdll.dll") {
                    native_base = reinterpret_cast<std::uintptr_t>(region.AllocationBase);
                    break;
                }
            }
            const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
            if (next <= cursor) break;
            cursor = next;
        }
        const auto native = GetModuleHandleW(L"ntdll.dll");
        const auto exit_thread = GetProcAddress(native, "RtlExitUserThread");
        if (!native_base || !exit_thread) return ErrorCode::dll_load_failed;
        const auto entry = native_base + reinterpret_cast<std::uintptr_t>(exit_thread) - reinterpret_cast<std::uintptr_t>(native);
        UniqueHandle loader{CreateRemoteThread(process.get(), nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(entry), nullptr, 0, nullptr)};
        DWORD code{};
        if (!loader.get()) return ErrorCode::dll_load_failed;
        const auto boot = wait_for_remote_thread(loader.get(), code);
        if (boot != ErrorCode::ok) return boot;
        timetransle::common::write_diagnostic("injector", "startupLoader", "ready");
    }

    const auto path_text = hook.path.wstring();
    const auto path_bytes = (path_text.size() + 1U) * sizeof(wchar_t);
    RemoteAllocation remote_path{process.get(), path_bytes};
    if (remote_path.get() == nullptr ||
        !write_remote(
            process.get(),
            remote_path.get(),
            path_text.c_str(),
            path_bytes)) {
        return ErrorCode::dll_load_failed;
    }

    const auto local_load_library =
        reinterpret_cast<const void*>(GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"),
            "LoadLibraryW"));
    const auto remote_load_library =
        remote_function_address(process_id, local_load_library);
    if (local_load_library == nullptr || !remote_load_library.has_value()) {
        return ErrorCode::dll_load_failed;
    }

    UniqueHandle load_thread{CreateRemoteThread(
        process.get(),
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(*remote_load_library),
        remote_path.get(),
        0,
        nullptr)};
    if (load_thread.get() == nullptr) {
        return ErrorCode::dll_load_failed;
    }

    DWORD load_result{};
    auto result = wait_for_remote_thread(load_thread.get(), load_result);
    if (result != ErrorCode::ok || load_result == 0) {
        return result == ErrorCode::ok ? ErrorCode::dll_load_failed : result;
    }

    const auto remote_hook_base =
        remote_module_base(process_id, hook.path.filename().wstring());
    if (!remote_hook_base.has_value()) {
        return ErrorCode::dll_load_failed;
    }

    const auto remote_initialize = *remote_hook_base + hook.initialize_offset;
    RemoteAllocation remote_request{
        process.get(),
        sizeof(initialize_request)};
    if (remote_request.get() == nullptr ||
        !write_remote(
            process.get(),
            remote_request.get(),
            &initialize_request,
            sizeof(initialize_request))) {
        return ErrorCode::dll_initialize_failed;
    }
    InitializeRequestV1 verification;
    SIZE_T verified_bytes{};
    if (ReadProcessMemory(
            process.get(),
            remote_request.get(),
            &verification,
            sizeof(verification),
            &verified_bytes) == FALSE ||
        verified_bytes != sizeof(verification) ||
        std::memcmp(
            &verification,
            &initialize_request,
            sizeof(verification)) != 0) {
        return ErrorCode::internal_error;
    }

    UniqueHandle initialize_thread{CreateRemoteThread(
        process.get(),
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_initialize),
        remote_request.get(),
        0,
        nullptr)};
    if (initialize_thread.get() == nullptr) {
        return ErrorCode::dll_initialize_failed;
    }

    DWORD initialize_result{};
    result = wait_for_remote_thread(initialize_thread.get(), initialize_result);
    if (result != ErrorCode::ok) {
        return result;
    }
    if (initialize_result != static_cast<DWORD>(HookApiResult::ok)) {
        switch (static_cast<HookApiResult>(initialize_result)) {
            case HookApiResult::already_initialized:
                return ErrorCode::already_attached;
            case HookApiResult::invalid_argument:
                return ErrorCode::authentication_failed;
            case HookApiResult::hook_setup_failed:
                return ErrorCode::hook_create_failed;
            case HookApiResult::clock_initialization_failed:
                return ErrorCode::dll_initialize_failed;
            case HookApiResult::not_initialized:
                return ErrorCode::target_identity_changed;
            case HookApiResult::inactive:
                return ErrorCode::unsupported_protocol;
            case HookApiResult::invalid_speed:
                return ErrorCode::hook_state_invalid;
            case HookApiResult::ok:
                return ErrorCode::dll_initialize_failed;
        }
        return ErrorCode::dll_initialize_failed;
    }
    return ErrorCode::ok;
}

[[nodiscard]] ErrorCode inspect_only(
    const std::uint32_t process_id,
    const std::uint64_t creation_time) {
    const auto hook = inspect_hook_binary();
    const auto inspection = timetransle::safety::inspect_target(
        process_id,
        creation_time,
        injector_machine(),
        hook.trusted);
    return inspection.decision.error;
}

}  // namespace

int wmain(const int argument_count, wchar_t** const arguments) {
    if (argument_count != 4 && argument_count != 5) {
        std::cerr
            << "usage: TimeTransleInjector <pid> <creation-time-hex> "
               "--inspect-only | --self-test | "
               "--controller-bootstrap <handle-hex>\n";
        return static_cast<int>(ErrorCode::invalid_message);
    }

    std::uint32_t process_id{};
    std::uint64_t creation_time{};
    if (!parse_u32(arguments[1], process_id) ||
        !parse_u64_hex(arguments[2], creation_time)) {
        return static_cast<int>(ErrorCode::invalid_message);
    }

    if (argument_count == 4 &&
        std::wstring_view{arguments[3]} == L"--inspect-only") {
        const auto result = inspect_only(process_id, creation_time);
        timetransle::common::write_diagnostic(
            "injector",
            "targetInspection",
            result == ErrorCode::ok ? "ready" : "rejected",
            result == ErrorCode::ok
                ? std::nullopt
                : std::optional<std::string_view>{
                      timetransle::protocol::to_string(result)});
        if (result != ErrorCode::ok) {
            std::cerr << timetransle::protocol::to_string(result) << '\n';
        }
        return static_cast<int>(result);
    }

    InitializeRequestV1 request;
    if (argument_count == 4 &&
        std::wstring_view{arguments[3]} == L"--self-test") {
        request.process_id = process_id;
        request.flags = timetransle::hook::product_bootstrap_flag;
    } else if (
        argument_count == 5 &&
        std::wstring_view{arguments[3]} ==
            L"--controller-bootstrap") {
        const auto controller_request = read_controller_bootstrap(
            arguments[4],
            process_id,
            creation_time);
        if (!controller_request.has_value()) {
            return static_cast<int>(
                ErrorCode::authentication_failed);
        }
        request = *controller_request;
        if (request.struct_size != sizeof(InitializeRequestV1) ||
            request.magic !=
                timetransle::hook::local_bootstrap_magic ||
            request.version !=
                timetransle::hook::local_bootstrap_version ||
            request.process_id != process_id ||
            (request.flags & ~timetransle::hook::enable_checkpoint_flag) !=
                (timetransle::hook::product_bootstrap_flag |
                 timetransle::hook::enable_ipc_flag)) {
            return static_cast<int>(
                ErrorCode::unsupported_protocol);
        }
    } else {
        return static_cast<int>(ErrorCode::invalid_message);
    }

    const auto result = inject(process_id, creation_time, request);
    timetransle::common::write_diagnostic(
        "injector",
        "loadLibraryAndHookInitialize",
        result == ErrorCode::ok ? "ok" : "failed",
        result == ErrorCode::ok
            ? std::nullopt
            : std::optional<std::string_view>{
                  timetransle::protocol::to_string(result)});
    if (result != ErrorCode::ok) {
        std::cerr << timetransle::protocol::to_string(result) << '\n';
    }
    return static_cast<int>(result);
}
