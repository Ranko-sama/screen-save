#include <windows.h>

LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"Screen Save native capture test", WS_CHILD | WS_VISIBLE,
            24, 20, 620, 24, window, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            24, 210, 400, 32, window, reinterpret_cast<HMENU>(101), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Confirm input", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            440, 210, 180, 32, window, reinterpret_cast<HMENU>(102), nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE,
            24, 270, 620, 24, window, reinterpret_cast<HMENU>(103), nullptr, nullptr);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        const COLORREF colors[] = {RGB(255,255,255), RGB(255,0,0), RGB(0,255,0), RGB(0,0,255), RGB(128,128,128), RGB(0,0,0)};
        for (int i = 0; i < 6; ++i) {
            RECT rectangle{24+i*100, 80, 124+i*100, 180};
            auto brush = CreateSolidBrush(colors[i]);
            FillRect(dc, &rectangle, brush);
            DeleteObject(brush);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 102) {
            wchar_t value[256];
            GetDlgItemTextW(window, 101, value, 256);
            SetDlgItemTextW(window, 103, value);
            SetDlgItemTextW(window, 102, L"Input confirmed");
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = Procedure;
    cls.lpszClassName = L"ScreenSaveNativeFixture";
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    HWND window = CreateWindowW(cls.lpszClassName, L"Screen Save - Native Fixture",
        WS_OVERLAPPEDWINDOW, 120, 120, 700, 380, nullptr, nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOW);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
