#include "timetransle/controller/application.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <commdlg.h>
#include <knownfolders.h>
#include <nlohmann/json.hpp>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>

#include <WebView2.h>

#include "timetransle/common/diagnostics.hpp"
#include "timetransle/controller/controller_session.hpp"
#include "timetransle/controller/hook_session.hpp"
#include "timetransle/controller/process_icon.hpp"
#include "timetransle/controller/resource_ids.h"
#include "timetransle/controller/web_message.hpp"
#include "timetransle/protocol/error_code.hpp"
#include "timetransle/safety/windows_inspector.hpp"

namespace timetransle::controller {
namespace {

using Json = nlohmann::json;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr wchar_t window_class_name[] = L"TimeTransle.Controller.Window";
constexpr wchar_t window_title[] = L"TimeTransle";
constexpr wchar_t virtual_host[] = L"app.timetransle.local";
constexpr wchar_t start_document[] =
    L"https://app.timetransle.local/index.html";
constexpr wchar_t webview2_download_page[] =
    L"https://developer.microsoft.com/microsoft-edge/webview2/";
constexpr UINT_PTR target_monitor_timer = 1;
constexpr UINT target_monitor_period_ms = 250;
constexpr UINT target_exited_message = WM_APP + 1;
constexpr UINT hook_event_message = WM_APP + 2;
constexpr UINT target_inspection_message = WM_APP + 3;
constexpr UINT global_shortcut_message = WM_APP + 4;
constexpr DWORD target_inspection_timeout_ms = 15'000;
constexpr wchar_t settings_registry_path[] =
    L"Software\\TimeTransle";
constexpr wchar_t window_placement_value[] = L"WindowPlacement";
constexpr DWORD window_placement_version = 1;
constexpr int default_window_size = 360;
constexpr int minimum_window_size = 320;
constexpr int maximum_window_size = 440;

class Application;
Application* keyboard_hook_owner{};

enum class GlobalShortcut : WPARAM {
    rate_1x = 1,
    rate_040 = 2,
    rate_250 = 3,
    toggle_window = 4,
    save_checkpoint = 5,
    load_checkpoint = 6,
};

struct StoredWindowPlacement {
    DWORD version{};
    LONG left{};
    LONG top{};
    DWORD logical_size{};
};

struct CoTaskMemStringDeleter {
    void operator()(wchar_t* value) const noexcept {
        CoTaskMemFree(value);
    }
};

using CoTaskMemString = std::unique_ptr<wchar_t, CoTaskMemStringDeleter>;

class UniqueHandle final {
public:
    explicit UniqueHandle(const HANDLE handle = nullptr) noexcept
        : handle_{handle} {}

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

struct TargetInspectionEvent {
    HookRequestIdentity request;
    safety::ProcessIdentity target;
    protocol::ErrorCode error{protocol::ErrorCode::internal_error};
};

[[nodiscard]] std::optional<std::filesystem::path> executable_directory() {
    std::wstring buffer(32'768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::nullopt;
    }
    buffer.resize(length);
    return std::filesystem::path{buffer}.parent_path();
}

[[nodiscard]] std::optional<std::filesystem::path> executable_path() {
    std::wstring buffer(32'768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::nullopt;
    }
    buffer.resize(length);
    return std::filesystem::path{buffer};
}

[[nodiscard]] std::wstring quote_argument(
    const std::wstring_view argument) {
    std::wstring output{L'"'};
    for (const auto character : argument) {
        if (character == L'"') {
            output.push_back(L'\\');
        }
        output.push_back(character);
    }
    output.push_back(L'"');
    return output;
}

[[nodiscard]] protocol::ErrorCode run_target_inspection(
    const safety::ProcessIdentity target,
    const std::filesystem::path& product_directory,
    const std::stop_token stop_token) {
    const wchar_t* injector_name = nullptr;
    if (target.machine == safety::MachineType::x64) {
        injector_name = L"TimeTransleInjector64.exe";
    } else if (target.machine == safety::MachineType::x86) {
        injector_name = L"TimeTransleInjector32.exe";
    } else {
        return protocol::ErrorCode::architecture_unsupported;
    }

    const auto injector_path = product_directory / injector_name;
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(injector_path, file_error) ||
        file_error) {
        return protocol::ErrorCode::architecture_unsupported;
    }

    wchar_t creation_text[32]{};
    if (swprintf_s(
            creation_text,
            L"%llx",
            static_cast<unsigned long long>(
                target.creation_time)) < 1) {
        return protocol::ErrorCode::internal_error;
    }

    auto command_line =
        quote_argument(injector_path.wstring()) + L" " +
        std::to_wstring(target.process_id) + L" " +
        creation_text + L" --inspect-only";
    command_line.push_back(L'\0');

    STARTUPINFOW startup{
        .cb = sizeof(STARTUPINFOW),
    };
    PROCESS_INFORMATION process{};
    if (CreateProcessW(
            injector_path.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            product_directory.c_str(),
            &startup,
            &process) == FALSE) {
        return GetLastError() == ERROR_ACCESS_DENIED
                   ? protocol::ErrorCode::access_denied
                   : protocol::ErrorCode::internal_error;
    }
    UniqueHandle process_handle{process.hProcess};
    UniqueHandle thread_handle{process.hThread};
    static_cast<void>(thread_handle);

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds{target_inspection_timeout_ms};
    while (!stop_token.stop_requested()) {
        const auto wait =
            WaitForSingleObject(process_handle.get(), 50);
        if (wait == WAIT_OBJECT_0) {
            DWORD exit_code{};
            if (GetExitCodeProcess(
                    process_handle.get(),
                    &exit_code) == FALSE) {
                return protocol::ErrorCode::internal_error;
            }
            return static_cast<protocol::ErrorCode>(exit_code);
        }
        if (wait == WAIT_FAILED) {
            return protocol::ErrorCode::internal_error;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            TerminateProcess(
                process_handle.get(),
                static_cast<UINT>(
                    protocol::ErrorCode::timeout));
            WaitForSingleObject(process_handle.get(), INFINITE);
            return protocol::ErrorCode::timeout;
        }
    }

    TerminateProcess(
        process_handle.get(),
        static_cast<UINT>(
            protocol::ErrorCode::controller_restarted));
    WaitForSingleObject(process_handle.get(), INFINITE);
    return protocol::ErrorCode::controller_restarted;
}

[[nodiscard]] std::optional<std::filesystem::path> ui_directory() {
    const auto executable = executable_directory();
    if (!executable.has_value()) {
        return std::nullopt;
    }
    const auto directory = *executable / L"resources" / L"ui";
    std::error_code error;
    if (!std::filesystem::is_regular_file(directory / L"index.html", error) ||
        error) {
        return std::nullopt;
    }
    return directory;
}

[[nodiscard]] int scale_for_dpi(
    const int logical_value,
    const UINT dpi) noexcept {
    return MulDiv(logical_value, static_cast<int>(dpi), 96);
}

[[nodiscard]] std::optional<StoredWindowPlacement>
load_window_placement() noexcept {
    StoredWindowPlacement placement{};
    DWORD size = sizeof(placement);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            settings_registry_path,
            window_placement_value,
            RRF_RT_REG_BINARY,
            nullptr,
            &placement,
            &size) != ERROR_SUCCESS ||
        size != sizeof(placement) ||
        placement.version != window_placement_version ||
        placement.logical_size <
            static_cast<DWORD>(minimum_window_size) ||
        placement.logical_size >
            static_cast<DWORD>(maximum_window_size)) {
        return std::nullopt;
    }

    const auto physical_size = scale_for_dpi(
        static_cast<int>(placement.logical_size),
        GetDpiForSystem());
    RECT bounds{
        .left = placement.left,
        .top = placement.top,
        .right = placement.left + physical_size,
        .bottom = placement.top + physical_size,
    };
    if (MonitorFromRect(&bounds, MONITOR_DEFAULTTONULL) == nullptr) {
        return std::nullopt;
    }
    return placement;
}

void save_window_placement(const HWND window) noexcept {
    if (window == nullptr || IsIconic(window) != FALSE ||
        IsZoomed(window) != FALSE) {
        return;
    }
    RECT bounds{};
    if (GetWindowRect(window, &bounds) == FALSE) {
        return;
    }
    const auto physical_size = bounds.right - bounds.left;
    const auto logical_size = std::clamp(
        MulDiv(physical_size, 96, static_cast<int>(GetDpiForWindow(window))),
        minimum_window_size,
        maximum_window_size);
    const StoredWindowPlacement placement{
        .version = window_placement_version,
        .left = bounds.left,
        .top = bounds.top,
        .logical_size = static_cast<DWORD>(logical_size),
    };
    RegSetKeyValueW(
        HKEY_CURRENT_USER,
        settings_registry_path,
        window_placement_value,
        REG_BINARY,
        &placement,
        sizeof(placement));
}

void enforce_square_resize(
    const WPARAM edge,
    RECT& bounds) noexcept {
    const auto width = bounds.right - bounds.left;
    const auto height = bounds.bottom - bounds.top;

    switch (edge) {
        case WMSZ_LEFT:
        case WMSZ_RIGHT:
            bounds.bottom = bounds.top + width;
            break;
        case WMSZ_TOP:
        case WMSZ_BOTTOM:
            bounds.right = bounds.left + height;
            break;
        case WMSZ_TOPLEFT: {
            const auto size = std::max(width, height);
            bounds.left = bounds.right - size;
            bounds.top = bounds.bottom - size;
            break;
        }
        case WMSZ_TOPRIGHT: {
            const auto size = std::max(width, height);
            bounds.right = bounds.left + size;
            bounds.top = bounds.bottom - size;
            break;
        }
        case WMSZ_BOTTOMLEFT: {
            const auto size = std::max(width, height);
            bounds.left = bounds.right - size;
            bounds.bottom = bounds.top + size;
            break;
        }
        case WMSZ_BOTTOMRIGHT: {
            const auto size = std::max(width, height);
            bounds.right = bounds.left + size;
            bounds.bottom = bounds.top + size;
            break;
        }
        default:
            break;
    }
}

[[nodiscard]] std::optional<std::wstring> user_data_directory() {
    PWSTR raw_path = nullptr;
    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_CREATE,
            nullptr,
            &raw_path))) {
        return std::nullopt;
    }
    const CoTaskMemString local_app_data{raw_path};
    const auto directory =
        std::filesystem::path{local_app_data.get()} /
        L"TimeTransle" /
        L"WebView2";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return std::nullopt;
    }
    return directory.wstring();
}

[[nodiscard]] std::optional<std::string> wide_to_utf8(
    const std::wstring_view value) {
    if (value.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    if (value.empty()) {
        return std::string{};
    }
    const auto required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return std::nullopt;
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required,
            nullptr,
            nullptr) != required) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<std::wstring> utf8_to_wide(
    const std::string_view value) {
    if (value.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    if (value.empty()) {
        return std::wstring{};
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::nullopt;
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required) != required) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::string make_identifier() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return "controller-session-unavailable";
    }
    std::array<wchar_t, 40> buffer{};
    const auto length = StringFromGUID2(
        guid,
        buffer.data(),
        static_cast<int>(buffer.size()));
    if (length <= 3) {
        return "controller-session-unavailable";
    }
    const std::wstring_view with_braces{
        buffer.data(),
        static_cast<std::size_t>(length - 1)};
    const auto converted = wide_to_utf8(
        with_braces.substr(1, with_braces.size() - 2));
    return converted.value_or("controller-session-unavailable");
}

[[nodiscard]] std::string_view session_status_name(
    const SessionStatus status) noexcept {
    switch (status) {
        case SessionStatus::idle:
            return "idle";
        case SessionStatus::target_selected:
            return "targetSelected";
        case SessionStatus::target_checking:
            return "checking";
        case SessionStatus::target_ready:
            return "ready";
        case SessionStatus::elevation_required:
            return "elevationRequired";
        case SessionStatus::attaching:
            return "attaching";
        case SessionStatus::hooked_syncing:
            return "hookedSyncing";
        case SessionStatus::hooked_synced:
            return "hookedSynced";
        case SessionStatus::hook_unknown:
            return "hookUnknown";
        case SessionStatus::target_exited:
            return "targetExited";
        case SessionStatus::webview_failed:
            return "webviewFailed";
        case SessionStatus::error:
            return "error";
    }
    return "error";
}

[[nodiscard]] std::string_view target_runtime_name(
    const protocol::TargetRuntime runtime) noexcept {
    return runtime == protocol::TargetRuntime::unity_mono ? "unityMono" : "generic";
}

[[nodiscard]] std::string_view time_backend_name(
    const protocol::TimeBackend backend) noexcept {
    return backend == protocol::TimeBackend::unity_mono ? "unityMono" : "win32Clock";
}

[[nodiscard]] std::string_view effect_state_name(
    const protocol::EffectState state) noexcept {
    switch (state) {
        case protocol::EffectState::unverified:
            return "unverified";
        case protocol::EffectState::pending:
            return "pending";
        case protocol::EffectState::applied:
            return "applied";
        case protocol::EffectState::degraded:
            return "degraded";
        case protocol::EffectState::failed:
            return "failed";
    }
    return "failed";
}

[[nodiscard]] std::string_view acceptance_error_name(
    const CommandAcceptance acceptance) noexcept {
    switch (acceptance) {
        case CommandAcceptance::accepted:
            return "none";
        case CommandAcceptance::stale_ui_sequence:
            return "staleUiSequence";
        case CommandAcceptance::wrong_target_generation:
            return "wrongTargetGeneration";
        case CommandAcceptance::invalid_state:
            return "invalidState";
    }
    return "invalidState";
}

[[nodiscard]] std::string_view web_message_error_name(
    const WebMessageError error) noexcept {
    switch (error) {
        case WebMessageError::none:
            return "none";
        case WebMessageError::untrusted_origin:
            return "untrustedOrigin";
        case WebMessageError::invalid_json:
            return "invalidJson";
        case WebMessageError::schema_violation:
            return "schemaViolation";
    }
    return "schemaViolation";
}

class Application final {
public:
    Application(
        HINSTANCE instance,
        int show_command,
        ControllerLaunchOptions options)
        : instance_{instance},
          show_command_{show_command},
          controller_session_id_{make_identifier()},
          pending_elevated_target_{
              std::move(options.elevated_target)} {
        common::initialize_diagnostics();
        common::write_diagnostic(
            "controller",
            "startup",
            "ready");
    }

    ~Application() {
        finish_launch(false);
        uninstall_keyboard_hook();
        stop_target_inspection();
        hook_session_.stop(false);
        drain_target_inspection_events();
        drain_hook_events();
        if (target_process_ != nullptr) {
            CloseHandle(target_process_);
        }
    }

    [[nodiscard]] int run() {
        const auto large_icon = static_cast<HICON>(LoadImageW(
            instance_,
            MAKEINTRESOURCEW(IDI_TIMETRANSLE),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_DEFAULTCOLOR | LR_SHARED));
        const auto small_icon = static_cast<HICON>(LoadImageW(
            instance_,
            MAKEINTRESOURCEW(IDI_TIMETRANSLE),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR | LR_SHARED));

        WNDCLASSEXW window_class{
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc = &Application::window_proc,
            .hInstance = instance_,
            .hIcon = large_icon,
            .hCursor = LoadCursorW(nullptr, IDC_ARROW),
            .hbrBackground = reinterpret_cast<HBRUSH>(
                GetStockObject(BLACK_BRUSH)),
            .lpszClassName = window_class_name,
            .hIconSm = small_icon,
        };
        if (RegisterClassExW(&window_class) == 0) {
            return 10;
        }

        const auto stored_placement = load_window_placement();
        const auto dpi = GetDpiForSystem();
        const auto logical_size = stored_placement.has_value()
                                      ? static_cast<int>(
                                            stored_placement->logical_size)
                                      : default_window_size;
        const auto physical_size = scale_for_dpi(logical_size, dpi);
        const auto left = stored_placement.has_value()
                              ? stored_placement->left
                              : CW_USEDEFAULT;
        const auto top = stored_placement.has_value()
                             ? stored_placement->top
                             : CW_USEDEFAULT;
        window_ = CreateWindowExW(
            0,
            window_class_name,
            window_title,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                WS_MINIMIZEBOX | WS_THICKFRAME,
            left,
            top,
            physical_size,
            physical_size,
            nullptr,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr) {
            return 11;
        }
        SendMessageW(
            window_,
            WM_SETICON,
            ICON_BIG,
            reinterpret_cast<LPARAM>(large_icon));
        SendMessageW(
            window_,
            WM_SETICON,
            ICON_SMALL,
            reinterpret_cast<LPARAM>(small_icon));

        keyboard_hook_owner = this;
        keyboard_hook_ = SetWindowsHookExW(
            WH_KEYBOARD_LL,
            &Application::keyboard_hook_proc,
            instance_,
            0);
        if (keyboard_hook_ == nullptr) {
            keyboard_hook_owner = nullptr;
            common::write_diagnostic(
                "controller",
                "globalShortcuts",
                "unavailable");
        }

        ShowWindow(window_, show_command_);
        UpdateWindow(window_);
        initialize_webview();

        MSG message{};
        while (true) {
            const auto result = GetMessageW(&message, nullptr, 0, 0);
            if (result == -1) {
                return 12;
            }
            if (result == 0) {
                return static_cast<int>(message.wParam);
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

private:
    static LRESULT CALLBACK keyboard_hook_proc(
        const int code,
        const WPARAM w_parameter,
        const LPARAM l_parameter) {
        auto* const application = keyboard_hook_owner;
        if (code != HC_ACTION || application == nullptr) {
            return CallNextHookEx(
                application == nullptr
                    ? nullptr
                    : application->keyboard_hook_,
                code,
                w_parameter,
                l_parameter);
        }

        const auto* const event =
            reinterpret_cast<const KBDLLHOOKSTRUCT*>(l_parameter);
        if (event == nullptr) {
            return CallNextHookEx(
                application->keyboard_hook_,
                code,
                w_parameter,
                l_parameter);
        }

        std::size_t index = 0;
        switch (event->vkCode) {
        case VK_F1:
            index = 0;
            break;
        case VK_F2:
            index = 1;
            break;
        case VK_F3:
            index = 2;
            break;
        case VK_ESCAPE:
            index = 3;
            break;
        case VK_F5:
            index = 4;
            break;
        case VK_F9:
            index = 5;
            break;
        default:
            return CallNextHookEx(
                application->keyboard_hook_,
                code,
                w_parameter,
                l_parameter);
        }

        const auto key_up =
            w_parameter == WM_KEYUP ||
            w_parameter == WM_SYSKEYUP;
        if (key_up && application->shortcut_key_down_[index]) {
            application->shortcut_key_down_[index] = false;
            return 1;
        }

        const auto key_down =
            w_parameter == WM_KEYDOWN ||
            w_parameter == WM_SYSKEYDOWN;
        if (!key_down ||
            (GetAsyncKeyState(VK_OEM_3) & 0x8000) == 0) {
            return CallNextHookEx(
                application->keyboard_hook_,
                code,
                w_parameter,
                l_parameter);
        }

        if (!application->shortcut_key_down_[index]) {
            application->shortcut_key_down_[index] = true;
            if (PostMessageW(
                    application->window_,
                    global_shortcut_message,
                    static_cast<WPARAM>(index + 1U),
                    0) == FALSE) {
                application->shortcut_key_down_[index] = false;
                return CallNextHookEx(
                    application->keyboard_hook_,
                    code,
                    w_parameter,
                    l_parameter);
            }
        }
        return 1;
    }

    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM w_parameter,
        LPARAM l_parameter) {
        Application* application = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(l_parameter);
            application =
                static_cast<Application*>(create->lpCreateParams);
            SetWindowLongPtrW(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(application));
            application->window_ = window;
        } else {
            application = reinterpret_cast<Application*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (application != nullptr) {
            return application->handle_message(
                message,
                w_parameter,
                l_parameter);
        }
        return DefWindowProcW(window, message, w_parameter, l_parameter);
    }

    LRESULT handle_message(
        const UINT message,
        const WPARAM w_parameter,
        const LPARAM l_parameter) {
        switch (message) {
            case WM_SIZE:
                resize_webview();
                return 0;
            case WM_GETMINMAXINFO: {
                auto* information =
                    reinterpret_cast<MINMAXINFO*>(l_parameter);
                const auto dpi = GetDpiForWindow(window_);
                const auto minimum =
                    scale_for_dpi(minimum_window_size, dpi);
                const auto maximum =
                    scale_for_dpi(maximum_window_size, dpi);
                information->ptMinTrackSize = {minimum, minimum};
                information->ptMaxTrackSize = {maximum, maximum};
                return 0;
            }
            case WM_SIZING:
                enforce_square_resize(
                    w_parameter,
                    *reinterpret_cast<RECT*>(l_parameter));
                return TRUE;
            case WM_DPICHANGED: {
                const auto* suggested =
                    reinterpret_cast<const RECT*>(l_parameter);
                const auto width = suggested->right - suggested->left;
                const auto height = suggested->bottom - suggested->top;
                const auto size = std::max(width, height);
                SetWindowPos(
                    window_,
                    nullptr,
                    suggested->left,
                    suggested->top,
                    size,
                    size,
                    SWP_NOACTIVATE | SWP_NOZORDER);
                return 0;
            }
            case WM_TIMER:
                if (w_parameter == target_monitor_timer) {
                    check_target_exit();
                }
                return 0;
            case target_exited_message:
                handle_target_exit();
                return 0;
            case hook_event_message:
                handle_hook_event(
                    std::unique_ptr<HookEvent>{
                        reinterpret_cast<HookEvent*>(l_parameter)});
                return 0;
            case target_inspection_message:
                handle_target_inspection(
                    std::unique_ptr<TargetInspectionEvent>{
                        reinterpret_cast<TargetInspectionEvent*>(
                            l_parameter)});
                return 0;
            case global_shortcut_message:
                handle_global_shortcut(
                    static_cast<GlobalShortcut>(w_parameter));
                return 0;
            case WM_PAINT:
                paint_native_status();
                return 0;
            case WM_DESTROY:
                KillTimer(window_, target_monitor_timer);
                uninstall_keyboard_hook();
                stop_target_inspection();
                hook_session_.stop(true);
                drain_target_inspection_events();
                drain_hook_events();
                save_window_placement(window_);
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(
            window_,
            message,
            w_parameter,
            l_parameter);
    }

    void initialize_webview() {
        ui_directory_ = ui_directory();
        if (!ui_directory_.has_value()) {
            fail_webview(
                L"界面资源缺失。请先在 ui 目录执行 npm run build，"
                L"再重新构建 Controller。");
            return;
        }
        const auto user_data = user_data_directory();
        if (!user_data.has_value()) {
            fail_webview(L"无法创建 WebView2 用户数据目录。");
            return;
        }

        const auto result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            user_data->c_str(),
            nullptr,
            Callback<
                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](
                    const HRESULT status,
                    ICoreWebView2Environment* environment) -> HRESULT {
                    if (FAILED(status) || environment == nullptr) {
                        fail_webview_runtime(
                            L"WebView2 初始化失败。请安装或修复 "
                            L"Microsoft Edge WebView2 Runtime。");
                        return S_OK;
                    }
                    return environment->CreateCoreWebView2Controller(
                        window_,
                        Callback<
                            ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](
                                const HRESULT controller_status,
                                ICoreWebView2Controller* controller)
                                -> HRESULT {
                                if (FAILED(controller_status) ||
                                    controller == nullptr) {
                                    fail_webview(
                                        L"WebView2 窗口创建失败。");
                                    return S_OK;
                                }
                                webview_controller_ = controller;
                                if (FAILED(controller->get_CoreWebView2(
                                        &webview_)) ||
                                    webview_ == nullptr) {
                                    fail_webview(
                                        L"WebView2 核心接口不可用。");
                                    return S_OK;
                                }
                                configure_webview();
                                return S_OK;
                            })
                            .Get());
                })
                .Get());
        if (FAILED(result)) {
            fail_webview_runtime(
                L"WebView2 Runtime 不可用。请安装后重试。");
        }
    }

    void configure_webview() {
        resize_webview();

        ComPtr<ICoreWebView2Settings> settings;
        if (FAILED(webview_->get_Settings(&settings)) ||
            settings == nullptr ||
            FAILED(settings->put_IsScriptEnabled(TRUE)) ||
            FAILED(settings->put_IsWebMessageEnabled(TRUE)) ||
            FAILED(settings->put_AreDefaultScriptDialogsEnabled(FALSE)) ||
            FAILED(settings->put_IsStatusBarEnabled(FALSE)) ||
            FAILED(settings->put_AreDevToolsEnabled(FALSE)) ||
            FAILED(settings->put_AreDefaultContextMenusEnabled(FALSE)) ||
            FAILED(settings->put_AreHostObjectsAllowed(FALSE)) ||
            FAILED(settings->put_IsZoomControlEnabled(FALSE))) {
            fail_webview(L"无法应用 WebView2 安全设置。");
            return;
        }

        ComPtr<ICoreWebView2_3> webview3;
        if (FAILED(webview_.As(&webview3)) ||
            FAILED(webview3->SetVirtualHostNameToFolderMapping(
                virtual_host,
                ui_directory_->c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
            fail_webview(L"无法映射本地受信界面资源。");
            return;
        }

        if (!register_webview_handlers()) {
            fail_webview(L"无法建立受信的 WebView2 消息边界。");
            return;
        }

        if (FAILED(webview_->Navigate(start_document))) {
            fail_webview(L"无法打开本地界面。");
        }
    }

    [[nodiscard]] bool register_webview_handlers() {
        const auto navigation_result = webview_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this](
                    ICoreWebView2*,
                    ICoreWebView2NavigationStartingEventArgs* args)
                    -> HRESULT {
                    LPWSTR raw_uri = nullptr;
                    if (FAILED(args->get_Uri(&raw_uri)) ||
                        raw_uri == nullptr) {
                        args->put_Cancel(TRUE);
                        return S_OK;
                    }
                    const CoTaskMemString uri{raw_uri};
                    if (std::wstring_view{uri.get()} != start_document) {
                        args->put_Cancel(TRUE);
                    }
                    return S_OK;
                })
                .Get(),
            &navigation_starting_token_);

        const auto navigation_completed_result =
            webview_->add_NavigationCompleted(
                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                    [this](
                        ICoreWebView2*,
                        ICoreWebView2NavigationCompletedEventArgs* args)
                        -> HRESULT {
                        BOOL succeeded = FALSE;
                        if (FAILED(args->get_IsSuccess(&succeeded)) ||
                            succeeded == FALSE) {
                            fail_webview(
                                L"本地界面加载失败。资源可能不完整。");
                        }
                        return S_OK;
                    })
                    .Get(),
                &navigation_completed_token_);

        const auto message_result = webview_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](
                    ICoreWebView2*,
                    ICoreWebView2WebMessageReceivedEventArgs* args)
                    -> HRESULT {
                    handle_web_message(args);
                    return S_OK;
                })
                .Get(),
            &web_message_token_);

        const auto permission_result = webview_->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](
                    ICoreWebView2*,
                    ICoreWebView2PermissionRequestedEventArgs* args)
                    -> HRESULT {
                    return args->put_State(
                        COREWEBVIEW2_PERMISSION_STATE_DENY);
                })
                .Get(),
            &permission_token_);

        const auto new_window_result = webview_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](
                    ICoreWebView2*,
                    ICoreWebView2NewWindowRequestedEventArgs* args)
                    -> HRESULT {
                    return args->put_Handled(TRUE);
                })
                .Get(),
            &new_window_token_);

        const auto process_failed_result = webview_->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [this](
                    ICoreWebView2*,
                    ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                    fail_webview(
                        L"WebView2 渲染进程失效。当前状态已标记为未知。");
                    return S_OK;
                })
                .Get(),
            &process_failed_token_);

        return SUCCEEDED(navigation_result) &&
               SUCCEEDED(navigation_completed_result) &&
               SUCCEEDED(message_result) &&
               SUCCEEDED(permission_result) &&
               SUCCEEDED(new_window_result) &&
               SUCCEEDED(process_failed_result);
    }

    void handle_web_message(
        ICoreWebView2WebMessageReceivedEventArgs* args) {
        LPWSTR raw_source = nullptr;
        LPWSTR raw_json = nullptr;
        if (FAILED(args->get_Source(&raw_source)) ||
            raw_source == nullptr ||
            FAILED(args->get_WebMessageAsJson(&raw_json)) ||
            raw_json == nullptr) {
            if (raw_source != nullptr) {
                CoTaskMemFree(raw_source);
            }
            if (raw_json != nullptr) {
                CoTaskMemFree(raw_json);
            }
            post_rejection(nullptr, "invalidMessage");
            return;
        }
        const CoTaskMemString source{raw_source};
        const CoTaskMemString json{raw_json};
        constexpr std::size_t maximum_message_characters = 65'536;
        const auto source_length =
            wcsnlen_s(source.get(), maximum_message_characters + 1);
        const auto json_length =
            wcsnlen_s(json.get(), maximum_message_characters + 1);
        if (source_length > maximum_message_characters ||
            json_length > maximum_message_characters) {
            post_rejection(nullptr, "messageTooLarge");
            return;
        }
        const auto utf8_source = wide_to_utf8(
            std::wstring_view{source.get(), source_length});
        const auto utf8_json = wide_to_utf8(
            std::wstring_view{json.get(), json_length});
        if (!utf8_source.has_value() || !utf8_json.has_value()) {
            post_rejection(nullptr, "invalidEncoding");
            return;
        }

        const auto validation =
            validate_web_message(*utf8_source, *utf8_json);
        if (validation.error != WebMessageError::none) {
            post_rejection(
                nullptr,
                web_message_error_name(validation.error));
            return;
        }
        dispatch_command(validation.command);
    }

    void dispatch_command(const UiCommand& command) {
        std::optional<safety::ProcessCatalogEntry> selected_target;
        if (command.type == UiCommandType::select_target) {
            selected_target = resolve_target(command);
            if (!selected_target.has_value()) {
                post_rejection(&command, "targetIdentityChanged");
                return;
            }
            stop_target_inspection();
            hook_session_.stop(true);
        }

        const auto acceptance = session_.accept(command);
        if (acceptance != CommandAcceptance::accepted) {
            post_rejection(
                &command,
                acceptance_error_name(acceptance));
            return;
        }

        switch (command.type) {
            case UiCommandType::launch_program:
                launch_program(command);
                return;
            case UiCommandType::save_checkpoint:
                submit_hook_command(command, HookCommandType::save_checkpoint);
                return;
            case UiCommandType::load_checkpoint:
                submit_hook_command(command, HookCommandType::load_checkpoint);
                return;
            case UiCommandType::list_processes:
                post_process_list(command);
                return;
            case UiCommandType::select_target:
                current_target_ = selected_target->identity;
                current_target_name_ = selected_target->image_name;
                if (pending_elevated_target_.has_value() &&
                    pending_elevated_target_->process_id ==
                        current_target_->process_id &&
                    pending_elevated_target_->creation_time ==
                        current_target_->creation_time) {
                    pending_elevated_target_.reset();
                }
                if (!begin_target_monitor(command)) {
                    current_target_.reset();
                    current_target_name_.clear();
                    session_.mark_target_exited();
                    post_rejection(&command, "targetUnavailable");
                    return;
                }
                post_status(command, "targetSelected");
                begin_target_inspection(command);
                return;
            case UiCommandType::get_status:
                post_status(command, "status");
                post_pending_elevated_target(command);
                return;
            case UiCommandType::attach_target:
                begin_attach(command);
                return;
            case UiCommandType::set_speed:
                submit_hook_command(
                    command,
                    HookCommandType::set_speed);
                return;
            case UiCommandType::pause:
                submit_hook_command(
                    command,
                    HookCommandType::pause);
                return;
            case UiCommandType::resume:
                submit_hook_command(
                    command,
                    HookCommandType::resume);
                return;
            case UiCommandType::reset:
                submit_hook_command(
                    command,
                    HookCommandType::reset);
                return;
            case UiCommandType::deactivate:
                submit_hook_command(
                    command,
                    HookCommandType::shutdown);
                return;
            case UiCommandType::relaunch_elevated:
                relaunch_elevated(command);
                return;
            case UiCommandType::open_diagnostics:
                open_diagnostics(command);
                return;
        }
    }

    void post_pending_elevated_target(
        const UiCommand& command) {
        if (!pending_elevated_target_.has_value() ||
            pending_elevated_target_announced_) {
            return;
        }
        const auto expected = *pending_elevated_target_;
        const auto processes = safety::enumerate_processes();
        const auto match = std::find_if(
            processes.begin(),
            processes.end(),
            [&expected](
                const safety::ProcessCatalogEntry& entry) {
                return entry.identity_complete &&
                       entry.identity.process_id ==
                           expected.process_id &&
                       entry.identity.creation_time ==
                           expected.creation_time &&
                       (entry.identity.machine ==
                            safety::MachineType::x86 ||
                        entry.identity.machine ==
                            safety::MachineType::x64);
            });
        if (match == processes.end()) {
            pending_elevated_target_.reset();
            post_rejection(
                &command,
                "targetIdentityChanged");
            return;
        }

        const auto name = wide_to_utf8(match->image_name);
        if (!name.has_value()) {
            pending_elevated_target_.reset();
            post_rejection(&command, "invalidEncoding");
            return;
        }
        Json response = common_response(
            command,
            "elevatedTargetReady");
        response["payload"] = {
            {"process",
             {
                 {"processId", match->identity.process_id},
                 {"creationTime",
                  std::to_string(
                      match->identity.creation_time)},
                 {"architecture",
                  match->identity.machine ==
                          safety::MachineType::x64
                      ? "x64"
                      : "x86"},
                 {"name", *name},
             }},
        };
        pending_elevated_target_announced_ = true;
        post_json(response.dump());
    }

    void relaunch_elevated(const UiCommand& command) {
        if (!current_target_.has_value()) {
            session_.mark_error();
            post_rejection(&command, "targetUnavailable");
            return;
        }
        const auto target = *current_target_;
        const auto processes = safety::enumerate_processes();
        const auto still_matches = std::any_of(
            processes.begin(),
            processes.end(),
            [&target](
                const safety::ProcessCatalogEntry& entry) {
                return entry.identity_complete &&
                       entry.identity.process_id ==
                           target.process_id &&
                       entry.identity.creation_time ==
                           target.creation_time;
            });
        if (!still_matches) {
            session_.mark_error();
            post_rejection(
                &command,
                "TARGET_IDENTITY_CHANGED");
            return;
        }

        const auto controller_path = executable_path();
        if (!controller_path.has_value()) {
            session_.mark_error();
            post_rejection(
                &command,
                protocol::to_string(
                    protocol::ErrorCode::elevation_failed));
            return;
        }
        const auto parameters =
            std::wstring{L"--elevated-target "} +
            std::to_wstring(target.process_id) + L" " +
            std::to_wstring(target.creation_time);
        SHELLEXECUTEINFOW launch{
            .cbSize = sizeof(SHELLEXECUTEINFOW),
            .fMask = SEE_MASK_NOCLOSEPROCESS,
            .hwnd = window_,
            .lpVerb = L"runas",
            .lpFile = controller_path->c_str(),
            .lpParameters = parameters.c_str(),
            .nShow = SW_SHOWNORMAL,
        };
        if (ShellExecuteExW(&launch) == FALSE) {
            if (GetLastError() == ERROR_CANCELLED) {
                common::write_diagnostic(
                    "controller",
                    "elevation",
                    "cancelled",
                    std::nullopt);
                Json response = common_response(
                    command,
                    "elevationCancelled");
                response["payload"] = Json::object();
                post_json(response.dump());
                return;
            }
            session_.mark_error();
            common::write_diagnostic(
                "controller",
                "elevation",
                "failed",
                protocol::to_string(
                    protocol::ErrorCode::elevation_failed));
            post_rejection(
                &command,
                protocol::to_string(
                    protocol::ErrorCode::elevation_failed));
            return;
        }
        if (launch.hProcess != nullptr) {
            CloseHandle(launch.hProcess);
        }
        common::write_diagnostic(
            "controller",
            "elevation",
            "launched",
            std::nullopt);
        PostMessageW(window_, WM_CLOSE, 0, 0);
    }

    void open_diagnostics(const UiCommand& command) {
        const auto directory =
            common::diagnostic_log_directory();
        if (!directory.has_value()) {
            post_rejection(&command, "diagnosticsUnavailable");
            return;
        }
        const auto result = ShellExecuteW(
            window_,
            L"open",
            directory->c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            post_rejection(&command, "diagnosticsUnavailable");
        }
    }

    void begin_target_inspection(const UiCommand& command) {
        if (!current_target_.has_value()) {
            session_.mark_error();
            post_rejection(&command, "targetUnavailable");
            return;
        }
        const auto directory = executable_directory();
        if (!directory.has_value()) {
            session_.mark_error();
            post_rejection(&command, "productLayoutInvalid");
            return;
        }

        session_.mark_target_checking();
        common::write_diagnostic(
            "controller",
            "targetInspection",
            "started",
            std::nullopt);
        post_status(command, "status");
        const auto target = *current_target_;
        const HookRequestIdentity request{
            .ui_epoch = command.ui_epoch,
            .ui_sequence = command.ui_sequence,
            .target_generation = command.target_generation,
        };
        inspection_thread_ = std::jthread{
            [this, target, directory = *directory, request](
                const std::stop_token stop_token) {
                const auto error = run_target_inspection(
                    target,
                    directory,
                    stop_token);
                if (stop_token.stop_requested()) {
                    return;
                }
                auto* const event =
                    new (std::nothrow) TargetInspectionEvent{
                        .request = request,
                        .target = target,
                        .error = error,
                    };
                if (event == nullptr ||
                    PostMessageW(
                        window_,
                        target_inspection_message,
                        0,
                        reinterpret_cast<LPARAM>(event)) == FALSE) {
                    delete event;
                }
            }};
    }

    void handle_target_inspection(
        std::unique_ptr<TargetInspectionEvent> event) {
        if (!event || !current_target_.has_value() ||
            event->request.target_generation !=
                session_.snapshot().target_generation ||
            event->target.process_id != current_target_->process_id ||
            event->target.creation_time !=
                current_target_->creation_time ||
            event->target.machine != current_target_->machine) {
            return;
        }

        const auto command = UiCommand{
            .type = UiCommandType::get_status,
            .ui_epoch = event->request.ui_epoch,
            .ui_sequence = event->request.ui_sequence,
            .target_generation =
                event->request.target_generation,
        };
        std::string_view result{"rejected"};
        if (event->error == protocol::ErrorCode::ok) {
            session_.mark_target_ready();
            result = "ready";
        } else if (
            event->error ==
            protocol::ErrorCode::integrity_mismatch) {
            session_.mark_elevation_required();
            result = "elevationRequired";
        } else {
            session_.mark_error();
        }
        common::write_diagnostic(
            "controller",
            "targetInspection",
            result,
            event->error == protocol::ErrorCode::ok
                ? std::nullopt
                : std::optional<std::string_view>{
                      protocol::to_string(event->error)});

        Json inspection_error = nullptr;
        if (event->error != protocol::ErrorCode::ok) {
            inspection_error =
                std::string{protocol::to_string(event->error)};
        }
        Json response = common_response(
            command,
            "targetInspection");
        response["payload"] = {
            {"result", result},
            {"error", std::move(inspection_error)},
            {"process",
             {
                 {"processId", current_target_->process_id},
                 {"creationTime",
                  std::to_string(
                      current_target_->creation_time)},
                 {"architecture",
                  current_target_->machine ==
                          safety::MachineType::x64
                      ? "x64"
                      : "x86"},
                 {"name",
                  wide_to_utf8(current_target_name_)
                      .value_or("Unknown")},
             }},
        };
        post_json(response.dump());
        if (event->error == protocol::ErrorCode::ok) {
            auto attach_command = command;
            attach_command.type = UiCommandType::attach_target;
            begin_attach(attach_command);
        }
    }

    void stop_target_inspection() noexcept {
        if (inspection_thread_.joinable()) {
            inspection_thread_.request_stop();
            inspection_thread_.join();
        }
    }

    void handle_global_shortcut(
        const GlobalShortcut shortcut) {
        if (shortcut == GlobalShortcut::toggle_window) {
            if (IsWindowVisible(window_) && !IsIconic(window_)) {
                ShowWindow(window_, SW_HIDE);
                return;
            }
            const auto foreground_window = GetForegroundWindow();
            const auto current_thread = GetCurrentThreadId();
            const auto foreground_thread =
                foreground_window != nullptr
                    ? GetWindowThreadProcessId(
                          foreground_window,
                          nullptr)
                    : 0;
            const auto input_attached =
                foreground_thread != 0 &&
                foreground_thread != current_thread &&
                AttachThreadInput(
                    current_thread,
                    foreground_thread,
                    TRUE) != FALSE;

            ShowWindow(window_, SW_RESTORE);
            SetWindowPos(
                window_,
                HWND_TOP,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            BringWindowToTop(window_);
            const auto foreground_set =
                SetForegroundWindow(window_) != FALSE;
            SetActiveWindow(window_);
            SetFocus(window_);

            if (input_attached) {
                AttachThreadInput(
                    current_thread,
                    foreground_thread,
                    FALSE);
            }

            if (!foreground_set) {
                FLASHWINFO flash{
                    sizeof(FLASHWINFO),
                    window_,
                    FLASHW_TRAY,
                    1,
                    0,
                };
                FlashWindowEx(&flash);
            }
            return;
        }

        if (shortcut == GlobalShortcut::save_checkpoint ||
            shortcut == GlobalShortcut::load_checkpoint) {
            post_json(Json{{"v", 1}, {"type", "shortcutInvoked"},
                {"controllerSessionId", controller_session_id_},
                {"payload", {{"action", shortcut == GlobalShortcut::save_checkpoint
                    ? "saveCheckpoint" : "loadCheckpoint"}}}}.dump());
            return;
        }
        const auto target_speed =
            shortcut == GlobalShortcut::rate_1x
                ? 1.0
                : shortcut == GlobalShortcut::rate_040
                    ? 0.4
                    : shortcut == GlobalShortcut::rate_250
                        ? 2.5
                        : 0.0;
        if (target_speed == 0.0 || webview_ == nullptr) {
            return;
        }
        const Json message{
            {"v", 1},
            {"type", "shortcutInvoked"},
            {"controllerSessionId", controller_session_id_},
            {"payload",
             {{"action", "toggleSpeed"}, {"targetSpeed", target_speed}}},
        };
        post_json(message.dump());
    }

    void uninstall_keyboard_hook() noexcept {
        if (keyboard_hook_ != nullptr) {
            UnhookWindowsHookEx(keyboard_hook_);
            keyboard_hook_ = nullptr;
        }
        if (keyboard_hook_owner == this) {
            keyboard_hook_owner = nullptr;
        }
        shortcut_key_down_.fill(false);
    }

    void drain_target_inspection_events() noexcept {
        if (window_ == nullptr) {
            return;
        }
        MSG message{};
        while (PeekMessageW(
                   &message,
                   window_,
                   target_inspection_message,
                   target_inspection_message,
                   PM_REMOVE) != FALSE) {
            delete reinterpret_cast<TargetInspectionEvent*>(
                message.lParam);
        }
    }

    void finish_launch(bool success) {
        if (launch_process_ == nullptr) return;
        if (success) ResumeThread(launch_thread_);
        else TerminateProcess(launch_process_, 1);
        CloseHandle(launch_thread_); CloseHandle(launch_process_);
        launch_thread_ = nullptr; launch_process_ = nullptr; launch_pid_ = 0;
    }

    void launch_program(const UiCommand& command) {
        std::array<wchar_t, 32768> filename{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"Windows programs (*.exe)\0*.exe\0\0";
        dialog.lpstrFile = filename.data(); dialog.nMaxFile = static_cast<DWORD>(filename.size());
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) return;
        finish_launch(false);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const auto directory = std::filesystem::path(filename.data()).parent_path().wstring();
        if (!CreateProcessW(filename.data(), nullptr, nullptr, nullptr, FALSE,
                CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &process)) {
            post_rejection(&command, "launchFailed"); return;
        }
        launch_process_ = process.hProcess; launch_thread_ = process.hThread;
        launch_pid_ = process.dwProcessId;
        const auto catalog = safety::enumerate_processes();
        const auto entry = std::find_if(catalog.begin(), catalog.end(), [this](const auto& item) {
            return item.identity.process_id == launch_pid_ && item.identity_complete;
        });
        if (entry == catalog.end()) {
            finish_launch(false); post_rejection(&command, "targetUnavailable"); return;
        }
        auto response = common_response(command, "programLaunched");
        response["payload"] = {{"process", {
            {"processId", entry->identity.process_id},
            {"creationTime", std::to_string(entry->identity.creation_time)},
            {"architecture", entry->identity.machine == safety::MachineType::x86 ? "x86" : "x64"},
            {"name", wide_to_utf8(entry->image_name).value_or("Program")}, {"icon", ""}}}};
        post_json(response.dump());
    }

    void post_checkpoint(const UiCommand& command, const ipc::CheckpointPayload& state) {
        auto response = common_response(command, "checkpointStatus");
        response["payload"] = {{"available", state.available}, {"hasSnapshot", state.has_snapshot},
            {"operation", state.operation},
            {"error", state.error == protocol::ErrorCode::ok ? Json(nullptr) : Json(protocol::to_string(state.error))}};
        post_json(response.dump());
    }

    void begin_attach(const UiCommand& command) {
        if (!current_target_.has_value()) {
            session_.mark_error();
            post_rejection(&command, "targetUnavailable");
            return;
        }
        const auto directory = executable_directory();
        if (!directory.has_value()) {
            session_.mark_error();
            post_rejection(&command, "productLayoutInvalid");
            return;
        }

        session_.mark_attaching();
        common::write_diagnostic(
            "controller",
            "attach",
            "started",
            std::nullopt);
        post_status(command, "status");
        const auto started = hook_session_.start(
            *current_target_,
            *directory,
            HookRequestIdentity{
                .ui_epoch = command.ui_epoch,
                .ui_sequence = command.ui_sequence,
                .target_generation =
                    command.target_generation,
            },
            [this](HookEvent event) {
                auto* const message =
                    new (std::nothrow) HookEvent{
                        std::move(event)};
                if (message == nullptr ||
                    PostMessageW(
                        window_,
                        hook_event_message,
                        0,
                        reinterpret_cast<LPARAM>(message)) ==
                        FALSE) {
                    delete message;
                }
            }, current_target_->process_id == launch_pid_);
        if (!started) {
            session_.mark_error();
            finish_launch(false);
            post_rejection(&command, "attachStartFailed");
        }
    }

    void submit_hook_command(
        const UiCommand& command,
        const HookCommandType type) {
        if (!hook_session_.submit(HookCommand{
                .type = type,
                .request = HookRequestIdentity{
                    .ui_epoch = command.ui_epoch,
                    .ui_sequence = command.ui_sequence,
                    .target_generation =
                        command.target_generation,
                },
                .speed = command.speed,
            })) {
            session_.mark_hook_unknown();
            post_status(command, "status");
            post_rejection(&command, "hookNotConnected");
        }
    }

    [[nodiscard]] UiCommand event_command(
        const HookEvent& event) const {
        return UiCommand{
            .type = UiCommandType::get_status,
            .ui_epoch = event.request.ui_epoch,
            .ui_sequence = event.request.ui_sequence,
            .target_generation =
                event.request.target_generation,
        };
    }

    void handle_hook_event(
        std::unique_ptr<HookEvent> event) {
        if (!event ||
            event->request.target_generation !=
                session_.snapshot().target_generation) {
            return;
        }
        const auto command = event_command(*event);
        if (event->type == HookEventType::attached) {
            common::write_diagnostic(
                "controller",
                "hookInitialize",
                "attached",
                std::nullopt);
            common::write_diagnostic(
                "controller",
                "timeBackend",
                time_backend_name(event->state.time_backend),
                std::nullopt);
            common::write_diagnostic(
                "controller",
                "timeEffect",
                effect_state_name(event->state.effect_state),
                std::nullopt);
            session_.mark_hook_syncing();
            if (!session_.accept_hook_ack(
                    event->request.target_generation,
                    event->state.command_sequence,
                    event->state.clock_revision,
                    event->state.configured_speed,
                    event->state.paused,
                    event->state.target_runtime,
                    event->state.time_backend,
                    event->state.effect_state)) {
                session_.mark_error();
                post_rejection(
                    &command,
                    "hookStateInvalid");
                return;
            }
            finish_launch(true);
            checkpoint_state_ = event->state.checkpoint;
            post_status(command, "status");
            post_checkpoint(command, checkpoint_state_);
            return;
        }

        if (event->type ==
            HookEventType::command_applied) {
            if (!session_.accept_hook_ack(
                    event->request.target_generation,
                    event->state.command_sequence,
                    event->state.clock_revision,
                    event->state.configured_speed,
                    event->state.paused,
                    event->state.target_runtime,
                    event->state.time_backend,
                    event->state.effect_state)) {
                post_rejection(
                    &command,
                    "staleHookAcknowledgement");
                return;
            }
            if (event->command_type ==
                HookCommandType::shutdown) {
                if (event->state.effect_state ==
                    protocol::EffectState::pending) {
                    post_status(command, "status");
                    return;
                }
                session_.mark_hook_unknown();
                post_status(command, "status");
                return;
            }
            if (event->command_type == HookCommandType::save_checkpoint ||
                event->command_type == HookCommandType::load_checkpoint ||
                event->command_type == HookCommandType::get_checkpoint_status) {
                checkpoint_state_ = event->state.checkpoint;
                post_status(command, "status");
                post_checkpoint(command, checkpoint_state_);
                return;
            }
            Json response = common_response(
                command,
                "speedApplied");
            response["commandSequence"] =
                event->state.command_sequence;
            response["clockRevision"] =
                event->state.clock_revision;
            response["payload"] = {
                {"configuredSpeed",
                 event->state.configured_speed},
                {"paused", event->state.paused},
                {"effectiveSpeed",
                 event->state.effective_speed},
                {"targetRuntime",
                 target_runtime_name(event->state.target_runtime)},
                {"timeBackend",
                 time_backend_name(event->state.time_backend)},
                {"effectState",
                 effect_state_name(event->state.effect_state)},
            };
            common::write_diagnostic(
                "controller",
                "timeBackend",
                time_backend_name(event->state.time_backend),
                std::nullopt);
            common::write_diagnostic(
                "controller",
                "timeEffect",
                effect_state_name(event->state.effect_state),
                std::nullopt);
            post_json(response.dump());
            return;
        }

        if (event->error >= protocol::ErrorCode::checkpoint_unavailable &&
            event->error <= protocol::ErrorCode::checkpoint_allocation_outside_arena) {
            checkpoint_state_.operation = 0;
            checkpoint_state_.error = event->error;
            if (event->error == protocol::ErrorCode::checkpoint_empty ||
                event->error == protocol::ErrorCode::checkpoint_device_reset)
                checkpoint_state_.has_snapshot = false;
            if (event->error == protocol::ErrorCode::checkpoint_unavailable ||
                event->error == protocol::ErrorCode::checkpoint_allocation_outside_arena)
                checkpoint_state_.available = false;
            post_checkpoint(command, checkpoint_state_);
            return;
        }
        finish_launch(false);
        checkpoint_state_ = {};
        post_checkpoint(command, checkpoint_state_);
        const auto error =
            protocol::to_string(event->error);
        common::write_diagnostic(
            "controller",
            event->type == HookEventType::disconnected
                ? "hookDisconnect"
                : "hookInitialize",
            "failed",
            error);
        if (event->type ==
            HookEventType::disconnected) {
            session_.mark_hook_unknown();
        } else if (session_.snapshot().status ==
                   SessionStatus::attaching) {
            session_.mark_error();
        } else {
            session_.mark_hook_unknown();
        }
        post_status(command, "status");
        post_rejection(&command, error);
    }

    void drain_hook_events() noexcept {
        if (window_ == nullptr) {
            return;
        }
        MSG message{};
        while (PeekMessageW(
                   &message,
                   window_,
                   hook_event_message,
                   hook_event_message,
                   PM_REMOVE) != FALSE) {
            delete reinterpret_cast<HookEvent*>(
                message.lParam);
        }
    }

    [[nodiscard]] std::optional<safety::ProcessCatalogEntry>
    resolve_target(
        const UiCommand& command) const {
        const auto processes = safety::enumerate_processes();
        const auto match = std::find_if(
            processes.begin(),
            processes.end(),
            [&command](const safety::ProcessCatalogEntry& entry) {
                return entry.identity_complete &&
                       entry.identity.process_id == command.process_id &&
                       entry.identity.creation_time ==
                           command.creation_time &&
                       (entry.identity.machine ==
                            safety::MachineType::x86 ||
                        entry.identity.machine ==
                            safety::MachineType::x64);
            });
        return match == processes.end()
                   ? std::nullopt
                   : std::optional{*match};
    }

    [[nodiscard]] bool begin_target_monitor(
        const UiCommand& command) {
        HANDLE process = OpenProcess(
            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            command.process_id);
        if (process == nullptr) {
            return false;
        }
        if (target_process_ != nullptr) {
            CloseHandle(target_process_);
        }
        target_process_ = process;
        SetTimer(
            window_,
            target_monitor_timer,
            target_monitor_period_ms,
            nullptr);
        return true;
    }

    void check_target_exit() {
        if (target_process_ != nullptr &&
            WaitForSingleObject(target_process_, 0) == WAIT_OBJECT_0) {
            PostMessageW(
                window_,
                target_exited_message,
                0,
                0);
        }
    }

    void handle_target_exit() {
        if (target_process_ == nullptr) {
            return;
        }
        stop_target_inspection();
        hook_session_.stop(false);
        CloseHandle(target_process_);
        target_process_ = nullptr;
        current_target_.reset();
        current_target_name_.clear();
        KillTimer(window_, target_monitor_timer);
        const auto generation = session_.snapshot().target_generation;
        session_.mark_target_exited();
        common::write_diagnostic(
            "controller",
            "targetMonitor",
            "targetExited",
            protocol::to_string(
                protocol::ErrorCode::target_exited));
        Json message{
            {"v", 1},
            {"type", "targetExited"},
            {"controllerSessionId", controller_session_id_},
            {"targetGeneration", generation},
            {"payload", Json::object()},
        };
        post_json(message.dump());
    }

    void post_process_list(const UiCommand& command) {
        Json processes = Json::array();
        for (const auto& entry : safety::enumerate_processes()) {
            if (!entry.identity_complete ||
                (entry.identity.machine != safety::MachineType::x86 &&
                 entry.identity.machine != safety::MachineType::x64)) {
                continue;
            }
            const auto name = wide_to_utf8(entry.image_name);
            if (!name.has_value()) {
                continue;
            }
            auto icon =
                entry.image_path.empty()
                    ? std::string{}
                    : extract_process_icon_data_url(entry.image_path);
            processes.push_back({
                {"processId", entry.identity.process_id},
                {"creationTime",
                 std::to_string(entry.identity.creation_time)},
                {"architecture",
                 entry.identity.machine == safety::MachineType::x64
                     ? "x64"
                     : "x86"},
                {"name", *name},
                {"icon", std::move(icon)},
            });
        }

        Json response = common_response(command, "processList");
        response["payload"] = {
            {"processes", std::move(processes)},
        };
        post_json(response.dump());
    }

    void post_status(
        const UiCommand& command,
        const std::string_view type) {
        const auto& snapshot = session_.snapshot();
        Json payload{
            {"status", session_status_name(snapshot.status)},
            {"target", nullptr},
            {"applied", nullptr},
        };
        if (snapshot.target.has_value()) {
            payload["target"] = {
                {"processId", snapshot.target->process_id},
                {"creationTime",
                 std::to_string(snapshot.target->creation_time)},
            };
        }
        if (snapshot.applied.has_value()) {
            payload["applied"] = {
                {"configuredSpeed", snapshot.applied->speed},
                {"paused", snapshot.applied->paused},
                {"commandSequence",
                 snapshot.applied->command_sequence},
                {"clockRevision", snapshot.applied->clock_revision},
                {"targetRuntime",
                 target_runtime_name(snapshot.applied->target_runtime)},
                {"timeBackend",
                 time_backend_name(snapshot.applied->time_backend)},
                {"effectState",
                 effect_state_name(snapshot.applied->effect_state)},
            };
        }
        Json response = common_response(command, type);
        response["targetGeneration"] =
            snapshot.target_generation.empty()
                ? command.target_generation
                : snapshot.target_generation;
        response["payload"] = std::move(payload);
        post_json(response.dump());
    }

    [[nodiscard]] Json common_response(
        const UiCommand& command,
        const std::string_view type) const {
        return {
            {"v", 1},
            {"type", type},
            {"controllerSessionId", controller_session_id_},
            {"uiEpoch", command.ui_epoch},
            {"uiSequence", command.ui_sequence},
            {"targetGeneration", command.target_generation},
        };
    }

    void post_rejection(
        const UiCommand* command,
        const std::string_view error) {
        Json response{
            {"v", 1},
            {"type", "commandRejected"},
            {"controllerSessionId", controller_session_id_},
            {"error", error},
        };
        if (command != nullptr) {
            response["uiEpoch"] = command->ui_epoch;
            response["uiSequence"] = command->ui_sequence;
            response["targetGeneration"] =
                command->target_generation;
        }
        post_json(response.dump());
    }

    void post_json(const std::string& json) {
        if (webview_ == nullptr) {
            return;
        }
        const auto wide = utf8_to_wide(json);
        if (wide.has_value()) {
            webview_->PostWebMessageAsJson(wide->c_str());
        }
    }

    void fail_webview(const std::wstring_view message) {
        hook_session_.stop(true);
        session_.mark_webview_failed();
        native_status_.assign(message);
        if (webview_controller_ != nullptr) {
            webview_controller_->put_IsVisible(FALSE);
        }
        if (window_ != nullptr) {
            InvalidateRect(window_, nullptr, TRUE);
        }
    }

    void fail_webview_runtime(
        const std::wstring_view message) {
        fail_webview(message);
        if (window_ == nullptr ||
            MessageBoxW(
                window_,
                L"TimeTransle 需要 Microsoft Edge WebView2 Runtime。"
                L"\n\n是否打开 Microsoft 官方下载页面？",
                L"TimeTransle · WebView2 Runtime",
                MB_YESNO | MB_ICONERROR | MB_DEFBUTTON1) != IDYES) {
            return;
        }
        const auto launch_result = ShellExecuteW(
            window_,
            L"open",
            webview2_download_page,
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(launch_result) <= 32) {
            native_status_ =
                L"无法打开 WebView2 官方下载页面，请检查默认浏览器设置。";
            InvalidateRect(window_, nullptr, TRUE);
        }
    }

    void resize_webview() const {
        if (webview_controller_ == nullptr || window_ == nullptr) {
            return;
        }
        RECT bounds{};
        if (GetClientRect(window_, &bounds) != FALSE) {
            webview_controller_->put_Bounds(bounds);
        }
    }

    void paint_native_status() const {
        static constexpr std::array<std::wstring_view, 4>
            startup_messages{
                L"正在向无限光祈祷...",
                L"正在翻译新的故事...",
                L"正在接受大她者的凝视...",
                L"正在手冲咖啡...",
            };
        static const auto startup_message = startup_messages[
            static_cast<std::size_t>(
                GetTickCount64() % startup_messages.size())];

        PAINTSTRUCT paint{};
        const auto device = BeginPaint(window_, &paint);
        if (device == nullptr) {
            return;
        }
        RECT bounds{};
        GetClientRect(window_, &bounds);
        FillRect(
            device,
            &bounds,
            reinterpret_cast<HBRUSH>(
                GetStockObject(BLACK_BRUSH)));
        SetBkMode(device, TRANSPARENT);
        SetTextColor(device, RGB(238, 208, 162));
        const auto text = native_status_.empty()
                              ? startup_message
                              : std::wstring_view{native_status_};
        DrawTextW(
            device,
            text.data(),
            static_cast<int>(text.size()),
            &bounds,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                DT_END_ELLIPSIS);
        EndPaint(window_, &paint);
    }

    HINSTANCE instance_{};
    int show_command_{};
    HWND window_{};
    std::optional<std::filesystem::path> ui_directory_;
    ComPtr<ICoreWebView2Controller> webview_controller_;
    ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken navigation_starting_token_{};
    EventRegistrationToken navigation_completed_token_{};
    EventRegistrationToken web_message_token_{};
    EventRegistrationToken permission_token_{};
    EventRegistrationToken new_window_token_{};
    EventRegistrationToken process_failed_token_{};
    ControllerSession session_;
    HookSession hook_session_;
    std::jthread inspection_thread_;
    HHOOK keyboard_hook_{};
    std::array<bool, 6> shortcut_key_down_{};
    std::string controller_session_id_;
    std::wstring native_status_;
    HANDLE target_process_{};
    HANDLE launch_process_{}, launch_thread_{};
    DWORD launch_pid_{};
    ipc::CheckpointPayload checkpoint_state_{};
    std::optional<safety::ProcessIdentity> current_target_;
    std::wstring current_target_name_;
    std::optional<safety::ProcessIdentity> pending_elevated_target_;
    bool pending_elevated_target_announced_{};
};

}  // namespace

int run_controller(
    HINSTANCE instance,
    const int show_command,
    ControllerLaunchOptions options) {
    const auto initialization =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialization)) {
        return 20;
    }

    int result = 0;
    {
        Application application{
            instance,
            show_command,
            std::move(options)};
        result = application.run();
    }
    CoUninitialize();
    return result;
}

int run_controller_self_test() {
    static_assert(sizeof(void*) == 8);
    const auto initialization =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialization)) {
        return 30;
    }

    int result = 0;
    LPWSTR raw_version = nullptr;
    if (FAILED(GetAvailableCoreWebView2BrowserVersionString(
            nullptr,
            &raw_version)) ||
        raw_version == nullptr ||
        *raw_version == L'\0') {
        result = 31;
    } else if (!ui_directory().has_value()) {
        result = 32;
    }
    CoTaskMemFree(raw_version);
    CoUninitialize();
    return result;
}

}  // namespace timetransle::controller
