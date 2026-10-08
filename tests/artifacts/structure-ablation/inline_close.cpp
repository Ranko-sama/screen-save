#include <windows.h>
#include <shellapi.h>
#include <magnification.h>
#include <wmistr.h>
#include <cstring>

constexpr UINT TrayMessage = WM_APP + 1;
MAGCOLOREFFECT originalColors;
HDC display;
WORD originalGamma[3][256];
enum class DimmingMode { Matrix, Gamma };
DimmingMode dimmingMode = DimmingMode::Matrix;
bool isDark = false;
int brightnessBeforeDark;

// This laptop exposes one 296-byte WNODE with one display instance.
// Native WMI exports are resolved locally; this is not a portable brightness API.
HMODULE brightnessLibrary;
HANDLE brightnessDataBlock, brightnessMethodBlock;
ULONG (WINAPI* nativeOpen)(GUID*, ULONG, HANDLE*);
ULONG (WINAPI* nativeQuery)(HANDLE, ULONG*, void*);
ULONG (WINAPI* nativeClose)(HANDLE);
ULONG (WINAPI* nativeExecute)(HANDLE, const wchar_t*, ULONG, ULONG, void*, ULONG*, void*);
wchar_t brightnessInstance[128];

void OpenBrightness() {
    brightnessLibrary = LoadLibraryW(L"advapi32.dll");
    nativeOpen = reinterpret_cast<decltype(nativeOpen)>(GetProcAddress(brightnessLibrary, "WmiOpenBlock"));
    nativeQuery = reinterpret_cast<decltype(nativeQuery)>(GetProcAddress(brightnessLibrary, "WmiQueryAllDataW"));
    nativeClose = reinterpret_cast<decltype(nativeClose)>(GetProcAddress(brightnessLibrary, "WmiCloseBlock"));
    nativeExecute = reinterpret_cast<decltype(nativeExecute)>(GetProcAddress(brightnessLibrary, "WmiExecuteMethodW"));
    GUID data{0xd43412ac, 0x67f9, 0x4fbb, {0xa0, 0x81, 0x17, 0x52, 0xa2, 0xc3, 0x3e, 0x84}};
    GUID methods{0x479b20b4, 0x5559, 0x46fe, {0xbe, 0x97, 0x7d, 0x22, 0x21, 0x54, 0x42, 0x1f}};
    nativeOpen(&data, WMIGUID_QUERY, &brightnessDataBlock);
    nativeOpen(&methods, WMIGUID_EXECUTE, &brightnessMethodBlock);
    alignas(8) BYTE bytes[512]{};
    ULONG size = sizeof(bytes);
    nativeQuery(brightnessDataBlock, &size, bytes);
    auto node = reinterpret_cast<WNODE_ALL_DATA*>(bytes);
    auto nameOffsets = reinterpret_cast<ULONG*>(bytes + node->OffsetInstanceNameOffsets);
    const BYTE* name = bytes + nameOffsets[0];
    const USHORT nameBytes = *reinterpret_cast<const USHORT*>(name);
    memcpy(brightnessInstance, name + sizeof(USHORT), nameBytes);
    brightnessInstance[nameBytes / sizeof(wchar_t)] = 0;
}

int ReadBrightness() {
    alignas(8) BYTE bytes[512]{};
    ULONG size = sizeof(bytes);
    nativeQuery(brightnessDataBlock, &size, bytes);
    return bytes[reinterpret_cast<WNODE_ALL_DATA*>(bytes)->DataBlockOffset];
}

void SetBrightness(int brightness) {
    // WmiSetBrightness method 1: uint32 Timeout (0), then uint8 Brightness.
    BYTE parameters[5]{0, 0, 0, 0, static_cast<BYTE>(brightness)};
    ULONG result = 0, resultBytes = sizeof(result);
    nativeExecute(brightnessMethodBlock, brightnessInstance, 1,
        sizeof(parameters), parameters, &resultBytes, &result);
}

void ApplyOutput(DimmingMode mode, bool dark) {
    if (mode == DimmingMode::Gamma) {
        WORD ramp[3][256];
        // The 0.5 scale was verified on this laptop; keep the original curve.
        for (int channel = 0; channel < 3; ++channel)
            for (int entry = 0; entry < 256; ++entry)
                ramp[channel][entry] = static_cast<WORD>(
                    originalGamma[channel][entry] / (dark ? 2 : 1));
        SetDeviceGammaRamp(display, ramp);
    } else {
        auto colors = originalColors;
        if (dark)
            for (int row = 0; row < 5; ++row)
                for (int column = 0; column < 3; ++column)
                    colors.transform[row][column] = 0.0f;
        MagSetFullscreenColorEffect(&colors);
        MagShowSystemCursor(!dark);
        if (!dark) {
            // Refresh the cursor at its current physical pixel, without DPI rounding.
            POINT cursor;
            GetPhysicalCursorPos(&cursor);
            SetPhysicalCursorPos(cursor.x, cursor.y);
        }
    }
}

void ToggleBlack() {
    // Keep one restore snapshot for this dark session, including mode switches.
    if (!isDark) {
        brightnessBeforeDark = ReadBrightness();
        MagGetFullscreenColorEffect(&originalColors);
        GetDeviceGammaRamp(display, originalGamma);
    }
    isDark = !isDark;
    if (dimmingMode == DimmingMode::Gamma)
        SetBrightness(isDark ? 0 : brightnessBeforeDark);
    ApplyOutput(dimmingMode, isDark);
    SetThreadExecutionState(ES_CONTINUOUS |
        (isDark ? ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : 0));
}

void SelectMode(DimmingMode mode) {
    if (mode == dimmingMode) return;
    if (isDark) {
        ApplyOutput(mode, true);
        SetBrightness(mode == DimmingMode::Gamma ? 0 : brightnessBeforeDark);
        ApplyOutput(dimmingMode, false);
    }
    dimmingMode = mode;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_HOTKEY) {
        if (w == 3) ToggleBlack();
        return 0;
    }
    if (message == TrayMessage && (l == WM_RBUTTONUP || l == WM_LBUTTONUP)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, isDark ? L"恢复" :
            dimmingMode == DimmingMode::Matrix ? L"黑屏" : L"最暗");
        HMENU modes = CreatePopupMenu();
        AppendMenuW(modes, MF_STRING, 3, L"颜色矩阵（可全黑）");
        AppendMenuW(modes, MF_STRING, 4, L"Gamma（不能全黑）");
        CheckMenuRadioItem(modes, 3, 4, dimmingMode == DimmingMode::Matrix ? 3 : 4, MF_BYCOMMAND);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modes), L"调光方式");
        AppendMenuW(menu, MF_STRING, 2, L"退出");
        POINT cursor;
        GetCursorPos(&cursor);
        HWND foreground = GetForegroundWindow();
        SetForegroundWindow(window);
        const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
            cursor.x, cursor.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (GetForegroundWindow() == window) SetForegroundWindow(foreground);
        if (command) SendMessageW(window, WM_COMMAND, command, 0);
        return 0;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(w) == 1) ToggleBlack();
        if (LOWORD(w) == 2) SendMessageW(window, WM_CLOSE, 0, 0);
        if (LOWORD(w) == 3) SelectMode(DimmingMode::Matrix);
        if (LOWORD(w) == 4) SelectMode(DimmingMode::Gamma);
        return 0;
    }
    if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // This hidden window has no text input; disable IME for this thread only.
    ImmDisableIME(GetCurrentThreadId());
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = WindowProcedure;
    cls.lpszClassName = L"ScreenSave";
    RegisterClassW(&cls);
    // Invisible message receiver only; no covering or rendering window.
    HWND window = CreateWindowW(cls.lpszClassName, L"Screen Save", 0,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    // The fixed hotkey owns the display session through final restoration.
    if (!RegisterHotKey(window, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B')) {
        DestroyWindow(window);
        return 0;
    }
    OpenBrightness();
    MagInitialize();
    display = GetDC(nullptr);
    NOTIFYICONDATAW tray{};
    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray.szTip, L"Screen Save · Ctrl+Alt+B 最暗/恢复");
    Shell_NotifyIconW(NIM_ADD, &tray);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        DispatchMessageW(&message);
    }
    if (isDark) ToggleBlack();
    Shell_NotifyIconW(NIM_DELETE, &tray);
    MagUninitialize();
    ReleaseDC(nullptr, display);
    nativeClose(brightnessMethodBlock);
    nativeClose(brightnessDataBlock);
    FreeLibrary(brightnessLibrary);
    UnregisterHotKey(window, 3);
    DestroyWindow(window);
    return 0;
}
