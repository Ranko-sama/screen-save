// Developer-only observer for the running candidate's cursor refresh events.
#include <windows.h>
#include <cstdio>

POINT expectedPosition;
int moves, otherInputs, wrongPositions;

LRESULT CALLBACK Observe(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto& event = *reinterpret_cast<MSLLHOOKSTRUCT*>(data);
        if (event.flags & LLMHF_INJECTED) {
            if (message == WM_MOUSEMOVE) ++moves;
            else ++otherInputs;
            if (event.pt.x != expectedPosition.x || event.pt.y != expectedPosition.y)
                ++wrongPositions;
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

int main() {
    HWND app = FindWindowW(L"ScreenSave", L"Screen Save");
    if (!app) return 2;
    DWORD pid;
    GetWindowThreadProcessId(app, &pid);
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, Observe, GetModuleHandleW(nullptr), 0);
    if (!hook) return 2;

    struct Step { const char* name; UINT message; WPARAM command; int expectedMoves; };
    const Step steps[] = {
        {"matrix_on", WM_HOTKEY, 3, 0},
        {"matrix_off", WM_HOTKEY, 3, 1},
        {"matrix_on_again", WM_HOTKEY, 3, 0},
        {"matrix_to_gamma", WM_COMMAND, 4, 1},
        {"gamma_to_matrix", WM_COMMAND, 3, 0},
        {"matrix_exit", WM_CLOSE, 0, 1}
    };
    int failures = 0;
    for (const auto& step : steps) {
        moves = otherInputs = wrongPositions = 0;
        GetPhysicalCursorPos(&expectedPosition);
        HWND foreground = GetForegroundWindow();
        PostMessageW(app, step.message, step.command, 0);
        const ULONGLONG deadline = GetTickCount64() + 900;
        for (;;) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline) break;
            MsgWaitForMultipleObjects(0, nullptr, FALSE,
                static_cast<DWORD>(deadline - now), QS_ALLINPUT);
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        POINT after;
        GetPhysicalCursorPos(&after);
        bool positionUnchanged = after.x == expectedPosition.x && after.y == expectedPosition.y;
        bool focusUnchanged = foreground == GetForegroundWindow();
        bool ok = moves == step.expectedMoves && !otherInputs && !wrongPositions &&
            positionUnchanged && focusUnchanged;
        printf("%s %s moves=%d expected=%d other_input=%d wrong_positions=%d "
            "position=%ld,%ld->%ld,%ld focus_unchanged=%d\n", ok ? "PASS" : "FAIL",
            step.name, moves, step.expectedMoves, otherInputs, wrongPositions,
            expectedPosition.x, expectedPosition.y, after.x, after.y, focusUnchanged);
        if (!ok) ++failures;
        fflush(stdout);
    }
    UnhookWindowsHookEx(hook);
    WaitForSingleObject(process, 5000);
    DWORD exitCode;
    GetExitCodeProcess(process, &exitCode);
    if (exitCode != 0) ++failures;
    CloseHandle(process);
    printf("exit_code=%lu FAILURES=%d\n", exitCode, failures);
    return failures ? 1 : 0;
}
