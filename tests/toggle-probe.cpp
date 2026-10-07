// Developer integration test for the separate, compiled application.
#include "../screen-save.cpp"
#include <powrprof.h>
#include <cstdio>
#include <cstring>

struct State {
    MAGCOLOREFFECT colors;
    WORD gamma[3][256];
    int brightness;
    EXECUTION_STATE power;
};
State baseline;
HWND target;
HANDLE process;
int failures;

State Capture() {
    State state{};
    MagGetFullscreenColorEffect(&state.colors);
    GetDeviceGammaRamp(display, state.gamma);
    state.brightness = ReadBrightness();
    CallNtPowerInformation(SystemExecutionState, nullptr, 0, &state.power, sizeof(state.power));
    return state;
}

void Check(const char* name, int brightness, bool matrixDark, bool gammaDark, bool awake) {
    State state = Capture();
    bool colorsMatch = true, gammaMatch = true;
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 5; ++c) {
            float expected = matrixDark && c < 3 ? 0.0f : baseline.colors.transform[r][c];
            colorsMatch &= state.colors.transform[r][c] == expected;
        }
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i)
            gammaMatch &= state.gamma[c][i] == baseline.gamma[c][i] / (gammaDark ? 2 : 1);
    bool powerMatches = awake ? (state.power & 3) == 3 : state.power == baseline.power;
    bool ok = state.brightness == brightness && colorsMatch && gammaMatch && powerMatches;
    printf("%s %s hardware=%d expected=%d matrix=%d gamma=%d power=%lu\n",
        ok ? "PASS" : "FAIL", name, state.brightness, brightness, colorsMatch, gammaMatch, state.power);
    if (!ok) ++failures;
    fflush(stdout);
}

void Start(const wchar_t* executable) {
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION child{};
    BOOL ok = CreateProcessW(executable, nullptr, nullptr, nullptr, FALSE, 0,
        nullptr, nullptr, &startup, &child);
    if (!ok) { printf("FAIL CreateProcess error=%lu\n", GetLastError()); ++failures; return; }
    process = child.hProcess;
    CloseHandle(child.hThread);
    Sleep(700);
    target = FindWindowW(L"ScreenSave", L"Screen Save");
    if (!target) { puts("FAIL application window missing"); ++failures; }
}

void Action(UINT message, WPARAM command) {
    PostMessageW(target, message, command, 0);
    Sleep(650);
}

void Stop() {
    PostMessageW(target, WM_CLOSE, 0, 0);
    WaitForSingleObject(process, 5000);
    DWORD code;
    GetExitCodeProcess(process, &code);
    printf("%s exit_code=%lu\n", code == 0 ? "PASS" : "FAIL", code);
    if (code != 0) ++failures;
    CloseHandle(process);
    target = nullptr;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || FindWindowW(L"ScreenSave", L"Screen Save")) return 2;
    OpenBrightness();
    MagInitialize();
    display = GetDC(nullptr);
    baseline = Capture();
    printf("BASELINE hardware=%d power=%lu\n", baseline.brightness, baseline.power);
    SetBrightness(60);
    Sleep(650);
    Start(argv[1]);
    Check("startup", 60, false, false, false);

    bool pageDownFree = RegisterHotKey(nullptr, 7, MOD_CONTROL | MOD_ALT, VK_NEXT) != FALSE;
    if (pageDownFree) UnregisterHotKey(nullptr, 7);
    bool pageUpFree = RegisterHotKey(nullptr, 8, MOD_CONTROL | MOD_ALT, VK_PRIOR) != FALSE;
    if (pageUpFree) UnregisterHotKey(nullptr, 8);
    bool toggleFree = RegisterHotKey(nullptr, 9, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B') != FALSE;
    DWORD toggleError = toggleFree ? 0 : GetLastError();
    if (toggleFree) UnregisterHotKey(nullptr, 9);
    bool keysOk = pageDownFree && pageUpFree && !toggleFree && toggleError == ERROR_HOTKEY_ALREADY_REGISTERED;
    printf("%s hotkeys PageDown_free=%d PageUp_free=%d toggle_owned=%d error=%lu\n",
        keysOk ? "PASS" : "FAIL", pageDownFree, pageUpFree, !toggleFree, toggleError);
    if (!keysOk) ++failures;
    Action(WM_HOTKEY, 1);
    Action(WM_HOTKEY, 2);
    Check("removed_step_commands", 60, false, false, false);

    Action(WM_HOTKEY, 3);
    Check("matrix_on_no_hardware_change", 60, true, false, true);
    Action(WM_COMMAND, 4);
    Check("dark_matrix_to_gamma", 0, false, true, true);
    Action(WM_COMMAND, 4);
    Check("reselect_gamma", 0, false, true, true);
    Action(WM_COMMAND, 3);
    Check("dark_gamma_to_matrix", 60, true, false, true);
    PostMessageW(target, WM_COMMAND, 4, 0);
    PostMessageW(target, WM_COMMAND, 3, 0);
    Action(WM_COMMAND, 4);
    Check("rapid_mode_changes_keep_restore_value", 0, false, true, true);
    Action(WM_HOTKEY, 3);
    Check("gamma_off_restores_session_brightness", 60, false, false, false);

    SetBrightness(37);
    Sleep(650);
    Action(WM_HOTKEY, 3);
    Check("new_gamma_session", 0, false, true, true);
    Action(WM_HOTKEY, 3);
    Check("new_session_restores_37_not_startup_60", 37, false, false, false);
    Action(WM_HOTKEY, 3);
    Check("gamma_before_exit", 0, false, true, true);
    Stop();
    Check("exit_from_gamma", 37, false, false, false);

    Start(argv[1]);
    Action(WM_HOTKEY, 3);
    Check("restart_defaults_to_matrix", 37, true, false, true);
    SetBrightness(43);
    Sleep(650);
    Check("matrix_leaves_external_hardware_change", 43, true, false, true);
    Stop();
    Check("exit_from_matrix_keeps_hardware", 43, false, false, false);
    Start(argv[1]);
    Stop();
    Check("exit_without_activating", 43, false, false, false);

    // Restore the developer test's own temporary hardware changes.
    SetBrightness(baseline.brightness);
    MagSetFullscreenColorEffect(&baseline.colors);
    SetDeviceGammaRamp(display, baseline.gamma);
    Sleep(650);
    Check("test_cleanup", baseline.brightness, false, false, false);
    printf("FAILURES=%d\n", failures);
    ReleaseDC(nullptr, display);
    MagUninitialize();
    brightnessArguments->Release();
    wmi->Release();
    SysFreeString(brightnessMethodPath);
    CoUninitialize();
    return failures ? 1 : 0;
}
