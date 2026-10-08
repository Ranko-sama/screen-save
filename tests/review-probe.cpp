// Independent-process adversarial checks; excluded from the release.
// Keep the COM observer independent of the application's native WMI implementation.
#include "artifacts/memory-optimization/baseline.cpp"
#include <cstdio>
#include <cstring>

struct Snapshot { MAGCOLOREFFECT colors; WORD gamma[3][256]; int brightness; };
struct Child { HANDLE process; DWORD pid; HWND window; };
int failures;

Snapshot ReadState() {
    Snapshot state{};
    MagGetFullscreenColorEffect(&state.colors);
    GetDeviceGammaRamp(display, state.gamma);
    state.brightness = ReadBrightness();
    return state;
}

void Restore(Snapshot state) {
    MagSetFullscreenColorEffect(&state.colors);
    SetDeviceGammaRamp(display, state.gamma);
    SetBrightness(state.brightness);
    MagShowSystemCursor(TRUE);
    POINT point;
    GetPhysicalCursorPos(&point);
    SetPhysicalCursorPos(point.x, point.y);
    Sleep(650);
}

bool SameColors(const Snapshot& a, const Snapshot& b) {
    return !memcmp(&a.colors, &b.colors, sizeof(a.colors)) &&
        !memcmp(a.gamma, b.gamma, sizeof(a.gamma));
}

void Expect(const char* name, bool ok) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    fflush(stdout);
    if (!ok) ++failures;
}

BOOL CALLBACK FindChild(HWND window, LPARAM value) {
    auto& child = *reinterpret_cast<Child*>(value);
    DWORD pid;
    GetWindowThreadProcessId(window, &pid);
    wchar_t name[64];
    GetClassNameW(window, name, 64);
    if (pid == child.pid && !wcscmp(name, L"ScreenSave")) child.window = window;
    return TRUE;
}

Child Start(const wchar_t* path) {
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    BOOL ok = CreateProcessW(path, nullptr, nullptr, nullptr, FALSE, 0,
        nullptr, nullptr, &startup, &process);
    Expect("create_process", ok != FALSE);
    if (!ok) return {};
    CloseHandle(process.hThread);
    Child child{process.hProcess, process.dwProcessId, nullptr};
    Sleep(700);
    EnumWindows(FindChild, reinterpret_cast<LPARAM>(&child));
    return child;
}

void Action(const Child& child, UINT message, WPARAM command) {
    PostMessageW(child.window, message, command, 0);
    Sleep(650);
}

void Stop(Child& child) {
    if (WaitForSingleObject(child.process, 0) != WAIT_OBJECT_0)
        Action(child, WM_CLOSE, 0);
    Expect("normal_exit", WaitForSingleObject(child.process, 5000) == WAIT_OBJECT_0);
    CloseHandle(child.process);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || FindWindowW(L"ScreenSave", L"Screen Save")) return 2;
    OpenBrightness();
    MagInitialize();
    display = GetDC(nullptr);
    Snapshot original = ReadState();
    printf("BASELINE hardware=%d gamma_white=%u\n", original.brightness, original.gamma[0][255]);

    Child first = Start(argv[1]);
    Snapshot external = original;
    external.colors.transform[0][0] *= 0.8f;
    for (int c=0;c<3;++c)
        for (int i=0;i<256;++i)
            external.gamma[c][i] = static_cast<WORD>(original.gamma[c][i] * 3 / 4);
    Restore(external);
    Expect("external_test_state_accepted", SameColors(ReadState(), external));
    Action(first, WM_COMMAND, 4);
    Expect("inactive_choose_gamma_preserves_external_colors", SameColors(ReadState(), external));
    Restore(external);
    Action(first, WM_COMMAND, 3);
    Expect("inactive_choose_matrix_preserves_external_colors", SameColors(ReadState(), external));
    Restore(external);
    Action(first, WM_HOTKEY, 3);
    Action(first, WM_HOTKEY, 3);
    Expect("matrix_restores_session_colors", SameColors(ReadState(), external));
    Restore(external);
    Action(first, WM_COMMAND, 4);
    Restore(external);
    Action(first, WM_HOTKEY, 3);
    Snapshot gammaDarkState = ReadState();
    bool scaledGammaApplied = true;
    for (int c=0;c<3;++c)
        for (int i=0;i<256;++i)
            scaledGammaApplied &= gammaDarkState.gamma[c][i] == external.gamma[c][i] / 2;
    printf("NONDEFAULT_GAMMA before_white=%u dark_white=%u\n",
        external.gamma[0][255], gammaDarkState.gamma[0][255]);
    Expect("gamma_scales_session_table_after_external_change", scaledGammaApplied);
    Action(first, WM_HOTKEY, 3);
    Expect("gamma_restores_session_colors", SameColors(ReadState(), external));
    Stop(first);
    Restore(original);

    first = Start(argv[1]);
    Action(first, WM_HOTKEY, 3);
    const int handoffModes[] = {4, 3};
    for (int mode : handoffModes) {
        PostMessageW(first.window, WM_COMMAND, mode, 0);
        int fullOutputSamples = 0;
        const auto end = GetTickCount64() + 650;
        do {
            MAGCOLOREFFECT before{}, after{};
            WORD gamma[3][256];
            MagGetFullscreenColorEffect(&before);
            GetDeviceGammaRamp(display, gamma);
            MagGetFullscreenColorEffect(&after);
            if (!memcmp(&before, &after, sizeof(before)) &&
                !memcmp(&after, &original.colors, sizeof(after)) &&
                !memcmp(gamma, original.gamma, sizeof(gamma))) ++fullOutputSamples;
            Sleep(1);
        } while (GetTickCount64() < end);
        printf("TRANSITION target=%d unmodified_color_samples=%d\n", mode, fullOutputSamples);
        Expect("dark_handoff_has_no_full_color_gap", fullOutputSamples == 0);
    }
    Stop(first);
    Restore(original);

    for (int mode=0;mode<3;++mode) {
        printf("SECOND_INSTANCE_CASE=%d (0=normal 1=matrix 2=gamma)\n", mode);
        first = Start(argv[1]);
        if (mode == 2) Action(first, WM_COMMAND, 4);
        if (mode) Action(first, WM_HOTKEY, 3);
        Child second = Start(argv[1]);
        bool rejected = WaitForSingleObject(second.process, 0) == WAIT_OBJECT_0;
        Expect("second_instance_exits", rejected);
        if (mode) Action(first, WM_HOTKEY, 3);
        if (!rejected && second.window) {
            if (mode == 2) Action(second, WM_COMMAND, 4);
            Action(second, WM_HOTKEY, 3);
            Action(second, WM_HOTKEY, 3);
        }
        Expect("no_second_instance_color_poisoning", SameColors(ReadState(), original));
        Stop(second);
        Stop(first);
        Restore(original);
    }

    bool reserved = RegisterHotKey(nullptr, 99, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B') != FALSE;
    Expect("test_reserves_hotkey", reserved);
    Child blocked = Start(argv[1]);
    Expect("no_instance_without_restore_hotkey", WaitForSingleObject(blocked.process, 0) == WAIT_OBJECT_0);
    Stop(blocked);
    if (reserved) UnregisterHotKey(nullptr, 99);
    Expect("hotkey_conflict_preserves_colors", SameColors(ReadState(), original));
    Restore(original);
    Expect("final_cleanup", SameColors(ReadState(), original) && ReadBrightness() == original.brightness);
    printf("FAILURES=%d\n", failures);
    ReleaseDC(nullptr, display);
    MagUninitialize();
    brightnessArguments->Release();
    wmi->Release();
    SysFreeString(brightnessMethodPath);
    CoUninitialize();
    return failures ? 1 : 0;
}
