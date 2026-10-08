// Independent observer. Uses the immutable original implementation only for
// reading/restoring display state, never for the candidate's state management.
#include "artifacts/memory-optimization/baseline.cpp"
#include <psapi.h>
#include <powrprof.h>
#include <cstdio>
#include <cstring>

HANDLE child;
HWND target;
MAGCOLOREFFECT savedColors;
WORD savedGamma[3][256];
int savedBrightness, failures;

bool Matches(bool matrix, bool gamma, bool awake) {
    MAGCOLOREFFECT colors{};
    WORD ramp[3][256]{};
    MagGetFullscreenColorEffect(&colors);
    GetDeviceGammaRamp(display, ramp);
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 5; ++c)
            if (colors.transform[r][c] != (matrix && c < 3 ? 0 : savedColors.transform[r][c])) return false;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i)
            if (ramp[c][i] != savedGamma[c][i] / (gamma ? 2 : 1)) return false;
    EXECUTION_STATE power{};
    CallNtPowerInformation(SystemExecutionState, nullptr, 0, &power, sizeof(power));
    return (power & 3) == (awake ? 3u : 0u);
}

void Sample(const char* stage) {
    Sleep(500);
    for (int i = 0; i < 3; ++i) {
        PROCESS_MEMORY_COUNTERS_EX2 memory{};
        memory.cb = sizeof(memory);
        BOOL ok = GetProcessMemoryInfo(child, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
        FILETIME created, exited, kernel, user;
        GetProcessTimes(child, &created, &exited, &kernel, &user);
        ULARGE_INTEGER k{{kernel.dwLowDateTime, kernel.dwHighDateTime}};
        ULARGE_INTEGER u{{user.dwLowDateTime, user.dwHighDateTime}};
        printf("MEM,%s,%d,%llu,%llu,%llu,%.4f\n", stage, i,
            static_cast<unsigned long long>(memory.PrivateWorkingSetSize),
            static_cast<unsigned long long>(memory.PrivateUsage),
            static_cast<unsigned long long>(memory.WorkingSetSize), (k.QuadPart + u.QuadPart) / 10000.0);
        if (!ok || !memory.PrivateWorkingSetSize) ++failures;
        fflush(stdout);
        Sleep(250);
    }
}

void Transition(const char* name, UINT message, WPARAM command, bool matrix, bool gamma, bool awake) {
    LARGE_INTEGER frequency, started, ended;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    PostMessageW(target, message, command, 0);
    bool matched = false;
    const ULONGLONG deadline = GetTickCount64() + 5000;
    do {
        Sleep(2);
        matched = Matches(matrix, gamma, awake);
    } while (!matched && GetTickCount64() < deadline);
    QueryPerformanceCounter(&ended);
    const double outputMs = (ended.QuadPart - started.QuadPart) * 1000.0 / frequency.QuadPart;
    int brightness = ReadBrightness();
    // WMI can report the old brightness after the color/power state has changed.
    // Wait for the hardware readback before beginning the next dark session.
    while (brightness != (gamma ? 0 : 60) && GetTickCount64() < deadline) {
        Sleep(10);
        brightness = ReadBrightness();
    }
    QueryPerformanceCounter(&ended);
    bool ok = matched && brightness == (gamma ? 0 : 60);
    if (!ok) ++failures;
    printf("ACTION,%s,%.4f,%s,brightness=%d,readback_ms=%.4f\n", name,
        outputMs, ok ? "PASS" : "FAIL", brightness,
        (ended.QuadPart - started.QuadPart) * 1000.0 / frequency.QuadPart);
    fflush(stdout);
    Sleep(650);
    const int settledBrightness = ReadBrightness();
    printf("SETTLED,%s,brightness=%d\n", name, settledBrightness);
    if (settledBrightness != (gamma ? 0 : 60)) ++failures;
}

int wmain(int argc, wchar_t** argv) {
    // Retired after the user requested fewer, slower tests. Kept only to
    // reproduce archived measurements after an explicit future decision.
    if (argc != 3 || wcscmp(argv[2], L"--enable-retired-automatic-test")) {
        puts("Automatic display tests disabled. Use a read-only snapshot and manual hotkeys.");
        return 2;
    }
    if (FindWindowW(L"ScreenSave", L"Screen Save")) return 2;
    OpenBrightness();
    MagInitialize();
    display = GetDC(nullptr);
    savedBrightness = ReadBrightness();
    MagGetFullscreenColorEffect(&savedColors);
    GetDeviceGammaRamp(display, savedGamma);
    SetBrightness(60);
    Sleep(300);
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(argv[1], nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info)) return 3;
    child = info.hProcess;
    CloseHandle(info.hThread);
    Sleep(700);
    target = FindWindowW(L"ScreenSave", L"Screen Save");
    DWORD owner;
    GetWindowThreadProcessId(target, &owner);
    if (owner != info.dwProcessId) { puts("FAIL child window"); return 4; }
    puts("TYPE,stage,sample,private_resident_bytes,private_commit_bytes,working_bytes,cpu_total_ms");
    Sample("startup");
    PostMessageW(target, TrayMessage, 1, WM_RBUTTONUP);
    Sleep(250);
    PostMessageW(target, WM_CANCELMODE, 0, 0);
    Sleep(250);
    Sample("after_menu");
    Transition("first_matrix_on", WM_HOTKEY, 3, true, false, true);
    Sample("matrix_dark");
    Transition("first_matrix_off", WM_HOTKEY, 3, false, false, false);
    Sample("matrix_restored");
    PostMessageW(target, WM_COMMAND, 4, 0);
    Sleep(100);
    Transition("first_gamma_on", WM_HOTKEY, 3, false, true, true);
    Sample("gamma_dark");
    Transition("gamma_to_matrix", WM_COMMAND, 3, true, false, true);
    Transition("matrix_to_gamma", WM_COMMAND, 4, false, true, true);
    Transition("first_gamma_off", WM_HOTKEY, 3, false, false, false);
    Sample("gamma_restored");
    for (int i = 0; i < 10; ++i) {
        PostMessageW(target, WM_COMMAND, i % 2 ? 3 : 4, 0);
        Sleep(30);
        Transition("repeat_on", WM_HOTKEY, 3, i % 2 != 0, i % 2 == 0, true);
        Sleep(30);
        Transition("repeat_off", WM_HOTKEY, 3, false, false, false);
        Sleep(30);
    }
    Sample("after_repeated_use");
    PostMessageW(target, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(child, 10000) != WAIT_OBJECT_0) ++failures;
    DWORD exitCode;
    GetExitCodeProcess(child, &exitCode);
    if (exitCode || !Matches(false, false, false) || ReadBrightness() != 60) ++failures;
    SetBrightness(savedBrightness);
    MagSetFullscreenColorEffect(&savedColors);
    SetDeviceGammaRamp(display, savedGamma);
    CloseHandle(child);
    ReleaseDC(nullptr, display);
    MagUninitialize();
    brightnessArguments->Release();
    wmi->Release();
    SysFreeString(brightnessMethodPath);
    CoUninitialize();
    printf("FAILURES=%d\n", failures);
    return failures ? 1 : 0;
}
