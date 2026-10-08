// Disposable real-input target. Does not control ScreenSave or inject any input.
#include <windows.h>
#include <windowsx.h>
#include <cstdio>

FILE* logFile;
int leftClicks, rightClicks, drags;
bool dragging;
LRESULT CALLBACK TestWindow(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSEMOVE) {
        POINT physical{};
        GetPhysicalCursorPos(&physical);
        fprintf(logFile, "%llu,%u,%d,%d,%ld,%ld,%llu\n", GetTickCount64(), message,
            GET_X_LPARAM(l), GET_Y_LPARAM(l), physical.x, physical.y,
            static_cast<unsigned long long>(w));
        fflush(logFile);
        if (message == WM_LBUTTONDOWN) {
            if (GET_Y_LPARAM(l) >= 100 && GET_Y_LPARAM(l) < 240) {
                if (GET_X_LPARAM(l) < 400) ++leftClicks; else ++rightClicks;
            }
            if (GET_Y_LPARAM(l) >= 280 && GET_Y_LPARAM(l) < 400) {
                dragging = true;
                SetCapture(window);
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        if (message == WM_LBUTTONUP && dragging) {
            dragging = false;
            ++drags;
            ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT client;
        GetClientRect(window, &client);
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        SetBkMode(dc, TRANSPARENT);
        const wchar_t* title = L"Real Windows input target (no display control)";
        TextOutW(dc, 35, 25, title, lstrlenW(title));
        Rectangle(dc, 35, 100, 365, 240);
        Rectangle(dc, 435, 100, 765, 240);
        TextOutW(dc, 155, 155, L"Target A", 8);
        TextOutW(dc, 555, 155, L"Target B", 8);
        Rectangle(dc, 35, 280, 765, 400);
        TextOutW(dc, 250, 325, L"Drag left to right inside this box", 34);
        wchar_t status[128];
        swprintf_s(status, L"A clicks: %d     B clicks: %d     Completed drags: %d", leftClicks, rightClicks, drags);
        TextOutW(dc, 35, 435, status, lstrlenW(status));
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();
    if (_wfopen_s(&logFile, L"C:\\Codex Project\\screen-save\\tests\\artifacts\\recovery-fix\\real-window-events.csv", L"w")) return 2;
    fprintf(logFile, "tick,message,client_x,client_y,physical_x,physical_y,buttons\n");
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = TestWindow;
    cls.lpszClassName = L"ScreenSaveRealCursorTarget";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    HWND window = CreateWindowW(cls.lpszClassName, L"Screen Save - Native cursor test",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        100, 100, 840, 550, nullptr, nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOWNORMAL);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    fclose(logFile);
    return 0;
}
