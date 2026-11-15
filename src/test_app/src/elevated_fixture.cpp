#include <Windows.h>

namespace {

constexpr wchar_t fixture_class_name[] =
    L"TimeTransleElevatedProgramFixture";
constexpr wchar_t fixture_window_title[] =
    L"Elevated Program Fixture";

LRESULT CALLBACK window_procedure(
    const HWND window,
    const UINT message,
    const WPARAM word_parameter,
    const LPARAM long_parameter) noexcept {
    switch (message) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const auto device_context = BeginPaint(window, &paint);
            constexpr wchar_t message_text[] =
                L"High-integrity TimeTransle test target";
            TextOutW(
                device_context,
                24,
                28,
                message_text,
                static_cast<int>(
                    sizeof(message_text) /
                        sizeof(message_text[0]) -
                    1U));
            EndPaint(window, &paint);
            return 0;
        }
        default:
            return DefWindowProcW(
                window,
                message,
                word_parameter,
                long_parameter);
    }
}

}  // namespace

int WINAPI wWinMain(
    HINSTANCE const instance,
    HINSTANCE,
    wchar_t*,
    int const show_command) {
    const WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = window_procedure,
        .hInstance = instance,
        .hCursor =
            LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)),
        .hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = fixture_class_name,
    };
    if (RegisterClassExW(&window_class) == 0) {
        return 1;
    }

    const auto window = CreateWindowExW(
        0,
        fixture_class_name,
        fixture_window_title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        520,
        180,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        return 2;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
