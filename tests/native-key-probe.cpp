// Temporary, read-only diagnostic for the laptop's brightness keys.
// Records function/media keys, Consumer HID reports and brightness notifications.
#include <windows.h>
#include <initguid.h>
#include <powrprof.h>
#include <hidsdi.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <share.h>
#include <vector>

HWND probeWindow, output;
FILE* record;
ULONGLONG started;
constexpr UINT HookMessage = WM_APP + 1;

bool DiagnosticKey(DWORD key) {
    return (key >= VK_F1 && key <= VK_F24) ||
        (key >= VK_BROWSER_BACK && key <= VK_LAUNCH_APP2) || key >= 0xe9;
}

void Log(const char* format, ...) {
    char line[512], message[600];
    va_list args;
    va_start(args, format);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    va_end(args);
    sprintf_s(message, "%8llu ms  %s\r\n", GetTickCount64() - started, line);
    fputs(message, record);
    fflush(record);
    if (output) {
        SendMessageA(output, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        SendMessageA(output, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(message));
    }
}

LRESULT CALLBACK Keyboard(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        auto key = reinterpret_cast<KBDLLHOOKSTRUCT*>(l);
        // No file or UI work inside the time-limited hook callback.
        if (DiagnosticKey(key->vkCode))
            PostMessageW(probeWindow, HookMessage, key->vkCode,
                static_cast<LPARAM>(key->scanCode) | (static_cast<LPARAM>(key->flags) << 32));
    }
    return CallNextHookEx(nullptr, code, w, l);
}

void ReadInput(HRAWINPUT input) {
    UINT size = 0;
    GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    std::vector<BYTE> bytes(size);
    if (GetRawInputData(input, RID_INPUT, bytes.data(), &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) {
        Log("GetRawInputData error=%lu", GetLastError()); return;
    }
    auto raw = reinterpret_cast<RAWINPUT*>(bytes.data());
    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        const auto& key = raw->data.keyboard;
        if (DiagnosticKey(key.VKey))
            Log("RAW vk=%04x %s scan=%04x device=%p", key.VKey,
                key.Flags & RI_KEY_BREAK ? "up" : "down", key.MakeCode, raw->header.hDevice);
        return;
    }
    if (raw->header.dwType != RIM_TYPEHID) return;
    UINT length = 0;
    GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, nullptr, &length);
    std::vector<BYTE> descriptor(length);
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, descriptor.data(), &length) == UINT(-1)) {
        Log("RIDI_PREPARSEDDATA error=%lu", GetLastError()); return;
    }
    auto data = reinterpret_cast<PHIDP_PREPARSED_DATA>(descriptor.data());
    HIDP_CAPS caps{};
    const LONG capsStatus = HidP_GetCaps(data, &caps);
    Log("HID device=%p page=%04x usage=%04x reports=%lu bytes=%lu buttons=%u values=%u caps=0x%08lx",
        raw->header.hDevice, caps.UsagePage, caps.Usage, raw->data.hid.dwCount,
        raw->data.hid.dwSizeHid, caps.NumberInputButtonCaps, caps.NumberInputValueCaps, capsStatus);
    const ULONG capacity = HidP_MaxUsageListLength(HidP_Input, 0x0c, data);
    std::vector<USAGE> usages(capacity);
    for (DWORD report = 0; report < raw->data.hid.dwCount; ++report) {
        ULONG count = capacity;
        auto packet = reinterpret_cast<PCHAR>(raw->data.hid.bRawData + report * raw->data.hid.dwSizeHid);
        char hex[97]{};
        for (DWORD i = 0; i < raw->data.hid.dwSizeHid && i < 32; ++i)
            sprintf_s(hex + i * 3, sizeof(hex) - i * 3, "%02x ", static_cast<BYTE>(packet[i]));
        Log("HID report=%s", hex);
        const LONG usageStatus = HidP_GetUsages(HidP_Input, 0x0c, 0, usages.data(), &count,
            data, packet, raw->data.hid.dwSizeHid);
        Log("HidP_GetUsages status=0x%08lx count=%lu", usageStatus, count);
        if (usageStatus != HIDP_STATUS_SUCCESS) continue;
        for (ULONG i = 0; i < count; ++i)
            Log("HID usage=%04x device=%p", usages[i], raw->header.hDevice);
    }
}

LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == HookMessage) {
        const DWORD flags = static_cast<DWORD>(static_cast<ULONGLONG>(l) >> 32);
        Log("HOOK vk=%04llx %s scan=%04lx injected=%u", static_cast<ULONGLONG>(w),
            flags & LLKHF_UP ? "up" : "down", static_cast<DWORD>(l), !!(flags & LLKHF_INJECTED));
        return 0;
    }
    if (message == WM_APPCOMMAND)
        Log("APPCOMMAND command=%u device=%u", GET_APPCOMMAND_LPARAM(l), GET_DEVICE_LPARAM(l));
    if (message == WM_INPUT) ReadInput(reinterpret_cast<HRAWINPUT>(l));
    if (message == WM_POWERBROADCAST && w == PBT_POWERSETTINGCHANGE) {
        auto setting = reinterpret_cast<POWERBROADCAST_SETTING*>(l);
        if (setting->PowerSetting == GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS)
            Log("BRIGHTNESS value=%lu", *reinterpret_cast<DWORD*>(setting->Data));
        return TRUE;
    }
    if (message == WM_TIMER) { Log("TIME LIMIT: three minutes"); DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    wchar_t logPath[MAX_PATH];
    GetModuleFileNameW(nullptr, logPath, MAX_PATH);
    *wcsrchr(logPath, L'\\') = 0;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t* tail = wcsrchr(logPath, L'\\');
    swprintf_s(tail, MAX_PATH - (tail - logPath),
        L"\\tests\\artifacts\\native-key-probe-%04u%02u%02u-%02u%02u%02u-%lu.txt",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, GetCurrentProcessId());
    record = _wfsopen(logPath, L"w", _SH_DENYNO);
    if (!record) return 1;
    started = GetTickCount64();
    WNDCLASSW cls{};
    cls.lpfnWndProc = Procedure;
    cls.hInstance = instance;
    cls.lpszClassName = L"ScreenSaveNativeKeyProbe";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    probeWindow = CreateWindowW(cls.lpszClassName, L"Screen Save - Native brightness key test",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 880, 580,
        nullptr, nullptr, instance, nullptr);
    CreateWindowW(L"STATIC", L"先按 Fn+F5、Fn+F6 各一次，再直接按 F5、F6 各三次。\n用 F5 降到最低，再按 F5 五次，F6 一次，最后 Fn+F5、Fn+F6 各一次。\n只观察，不拦截、不改变亮度；3 分钟后自动关闭。",
        WS_CHILD | WS_VISIBLE, 16, 12, 825, 78, probeWindow, nullptr, instance, nullptr);
    output = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
        16, 100, 825, 425, probeWindow, nullptr, instance, nullptr);
    RAWINPUTDEVICE devices[] = {{1, 6, RIDEV_INPUTSINK, probeWindow},
        {0x0c, 0, RIDEV_PAGEONLY | RIDEV_INPUTSINK, probeWindow}};
    BOOL registered = RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
    Log("RawInput registration=%d error=%lu", registered, registered ? 0 : GetLastError());
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, Keyboard, instance, 0);
    Log("Keyboard hook=%s error=%lu", hook ? "ready" : "failed", hook ? 0 : GetLastError());
    auto notification = RegisterPowerSettingNotification(probeWindow,
        &GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS, DEVICE_NOTIFY_WINDOW_HANDLE);
    Log("Brightness notification=%s", notification ? "ready" : "failed");
    Log("READY - follow the instructions above; no keys are blocked.");
    SetTimer(probeWindow, 1, 180000, nullptr);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (hook) UnhookWindowsHookEx(hook);
    if (notification) UnregisterPowerSettingNotification(notification);
    output = nullptr;
    Log("STOPPED");
    fclose(record);
    return 0;
}
