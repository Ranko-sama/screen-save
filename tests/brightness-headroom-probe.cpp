// Developer-only experiment; not included in the released EXE.
// Reuse the app's WMI connection and Gamma scaling. No keyboard interception.
#include "../screen-save.cpp"
#include <initguid.h>
#include <powrprof.h>
#include <cstdio>
#include <cstring>

constexpr UINT BrightnessEvent = WM_APP + 20;
constexpr int Anchor = 1;
// This abandoned experiment keeps its own step state; the released app has none.
constexpr int DimSteps = 20;
int dimLevel = 0;
HWND probe;
ULONGLONG started, pendingSince;
int savedBrightness, decreases, increases, exerciseStep;
bool active, waitingForAnchor, selfTest, failed;

void Print(const char* action, int value) {
    printf("%6llu ms %s=%d\n", GetTickCount64() - started, action, value);
    fflush(stdout);
}

HRESULT WriteHardware(int value) {
    VARIANT argument{};
    argument.vt = VT_UI1;
    argument.bVal = static_cast<BYTE>(value);
    brightnessArguments->Put(L"Brightness", 0, &argument, 0);
    BSTR method = SysAllocString(L"WmiSetBrightness");
    ULONGLONG before = GetTickCount64();
    HRESULT status = wmi->ExecMethod(brightnessMethodPath, method,
        0, nullptr, brightnessArguments, nullptr, nullptr);
    printf("%6llu ms WRITE value=%d hresult=0x%08lx duration_ms=%llu\n",
        GetTickCount64() - started, value, status, GetTickCount64() - before);
    SysFreeString(method);
    fflush(stdout);
    if (FAILED(status)) { failed = true; PostQuitMessage(1); }
    else {
        int actual = ReadBrightness();
        printf("%6llu ms READBACK actual=%d expected=%d total_write_read_ms=%llu\n",
            GetTickCount64() - started, actual, value, GetTickCount64() - before);
        // The driver may complete asynchronously; this is a measurement, not a retry.
    }
    fflush(stdout);
    return status;
}

void ApplyGamma() {
    WORD ramp[3][256];
    for (int channel = 0; channel < 3; ++channel)
        for (int i = 0; i < 256; ++i)
            ramp[channel][i] = static_cast<WORD>(originalGamma[channel][i] * (40 - dimLevel) / 40);
    BOOL changed = SetDeviceGammaRamp(display, ramp);
    Print("GAMMA_OK", changed);
    if (!changed) { failed = true; PostQuitMessage(1); }
}

void Reanchor() {
    waitingForAnchor = true;
    pendingSince = GetTickCount64();
    WriteHardware(Anchor);
}

void Observe(int value, ULONGLONG delivered) {
    Print("EVENT", value);
    Print("dispatch_delay_ms", static_cast<int>(GetTickCount64() - delivered));
    if (!active) return;
    if (value == Anchor) {
        if (waitingForAnchor) {
            Print("ANCHOR_ACK_MS", static_cast<int>(GetTickCount64() - pendingSince));
            waitingForAnchor = false;
        }
        return;
    }
    if (waitingForAnchor) Print("EVENT_BEFORE_ANCHOR_ACK", value);
    if (value < Anchor) {
        ++decreases;
        dimLevel = min(DimSteps, dimLevel + 1);
    } else {
        ++increases;
        dimLevel = max(0, dimLevel - 1);
    }
    printf("%6llu ms STEP direction=%s down=%d up=%d dim=%d\n",
        GetTickCount64() - started, value < Anchor ? "DOWN" : "UP", decreases, increases, dimLevel);
    // Restore the hardware first; otherwise a slow Gamma call prolongs a bright flash.
    Reanchor();
    ApplyGamma();
}

LRESULT CALLBACK HeadroomWindow(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_POWERBROADCAST && w == PBT_POWERSETTINGCHANGE) {
        auto setting = reinterpret_cast<POWERBROADCAST_SETTING*>(l);
        if (setting->PowerSetting == GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS)
            PostMessageW(window, BrightnessEvent, *reinterpret_cast<DWORD*>(setting->Data),
                static_cast<LPARAM>(GetTickCount64()));
        return TRUE;
    }
    if (message == BrightnessEvent) {
        // WMI calls must run after returning from the cross-process power message.
        Observe(static_cast<int>(w), static_cast<ULONGLONG>(l));
        return 0;
    }
    if (message == WM_TIMER && w == 1) {
        KillTimer(window, 1);
        active = true;
        Reanchor();
        Print("READY_ANCHOR", Anchor);
        if (selfTest) SetTimer(window, 2, 1200, nullptr);
        return 0;
    }
    if (message == WM_TIMER && w == 2) {
        if (exerciseStep == 4) {
            failed = failed || decreases != 2 || increases != 2 || ReadBrightness() != Anchor;
            Print("SELFTEST_PASS", !failed);
            PostQuitMessage(failed ? 1 : 0);
        } else {
            int value = (exerciseStep++ % 2) ? 2 : 0;
            Print("INJECT_HARDWARE_CHANGE_NOT_KEY", value);
            WriteHardware(value);
        }
        return 0;
    }
    if ((message == WM_TIMER && w == 3) || message == WM_HOTKEY || message == WM_CLOSE) {
        if (selfTest) failed = true;
        Print("STOP_REQUEST", static_cast<int>(message));
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int main(int argc, char** argv) {
    selfTest = argc > 1 && strcmp(argv[1], "selftest") == 0;
    started = GetTickCount64();
    OpenBrightness();
    savedBrightness = ReadBrightness();
    display = GetDC(nullptr);
    if (!GetDeviceGammaRamp(display, originalGamma)) return 2;
    Print("SAVED_BRIGHTNESS", savedBrightness);
    Print("SELFTEST", selfTest);
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = HeadroomWindow;
    cls.lpszClassName = L"ScreenSaveHeadroomProbe";
    RegisterClassW(&cls);
    probe = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0,
        nullptr, nullptr, cls.hInstance, nullptr);
    auto notification = RegisterPowerSettingNotification(probe,
        &GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS, DEVICE_NOTIFY_WINDOW_HANDLE);
    Print("POWER_SUBSCRIPTION", notification != nullptr);
    BOOL exitKey = RegisterHotKey(probe, 1, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q');
    Print("EXIT_HOTKEY_CTRL_ALT_SHIFT_Q", exitKey);
    if (!exitKey) Print("EXIT_HOTKEY_ERROR", GetLastError());
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    SetTimer(probe, 1, 800, nullptr); // Let the initial power snapshot arrive first.
    SetTimer(probe, 3, selfTest ? 10000 : 180000, nullptr);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    active = false;
    UnregisterPowerSettingNotification(notification);
    UnregisterHotKey(probe, 1);
    BOOL gammaRestored = SetDeviceGammaRamp(display, originalGamma);
    WriteHardware(savedBrightness);
    Sleep(500); // One settling interval before the final restoration measurement.
    WORD currentGamma[3][256]{};
    BOOL gammaRead = GetDeviceGammaRamp(display, currentGamma);
    int currentBrightness = ReadBrightness();
    printf("RESTORED hardware=%d expected=%d gamma_set=%d gamma_exact=%d\n",
        currentBrightness, savedBrightness, gammaRestored,
        gammaRead && memcmp(currentGamma, originalGamma, sizeof(originalGamma)) == 0);
    printf("TOTAL down=%d up=%d dim=%d\n", decreases, increases, dimLevel);
    failed = failed || currentBrightness != savedBrightness || !gammaRestored ||
        !gammaRead || memcmp(currentGamma, originalGamma, sizeof(originalGamma)) != 0;
    fflush(stdout);
    SetThreadExecutionState(ES_CONTINUOUS);
    DestroyWindow(probe);
    ReleaseDC(nullptr, display);
    brightnessArguments->Release();
    wmi->Release();
    SysFreeString(brightnessMethodPath);
    CoUninitialize();
    return failed ? 1 : 0;
}
