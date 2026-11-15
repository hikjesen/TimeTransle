#include <Windows.h>

BOOL APIENTRY DllMain(
    const HMODULE module,
    const DWORD reason,
    LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }

    return TRUE;
}
