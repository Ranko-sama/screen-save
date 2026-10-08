// Developer-only real hotkey/menu test. Run only while no Screen Save is running.
#include <windows.h>
#include <magnification.h>
#include <cstdio>
#include <cwchar>

DWORD applicationPid;
HWND application, firstWindow, secondWindow;
MAGCOLOREFFECT originalColors;
int failures;

void Pump(DWORD milliseconds) {
    const ULONGLONG end = GetTickCount64() + milliseconds;
    do {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    } while (GetTickCount64() < end);
}

bool Check(const char* name, bool ok) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    fflush(stdout);
    if (!ok) ++failures;
    return ok;
}

DWORD Owner(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid;
}

BOOL CALLBACK FindPopup(HWND window, LPARAM result) {
    wchar_t name[32];
    GetClassNameW(window, name, ARRAYSIZE(name));
    if (IsWindowVisible(window) && Owner(window) == applicationPid &&
        wcscmp(name, L"#32768") == 0) {
        *reinterpret_cast<HWND*>(result) = window;
        return FALSE;
    }
    return TRUE;
}

HWND Popup() {
    HWND result = nullptr;
    EnumWindows(FindPopup, reinterpret_cast<LPARAM>(&result));
    return result;
}

void MenuState(const char* stage, WORD key = 0) {
    HWND popup = Popup();
    HWND foreground = GetForegroundWindow();
    GUITHREADINFO thread{sizeof(thread)};
    GetGUIThreadInfo(GetWindowThreadProcessId(application, nullptr), &thread);
    printf("MENU %s key=%u popup=%p foreground=%p foreground_pid=%lu focus=%p flags=%lu",
        stage, key, static_cast<void*>(popup), static_cast<void*>(foreground),
        Owner(foreground), static_cast<void*>(thread.hwndFocus), thread.flags);
    MENUBARINFO info{sizeof(info)};
    if (popup && GetMenuBarInfo(popup, OBJID_CLIENT, 0, &info)) {
        for (int i = 0; i < GetMenuItemCount(info.hMenu); ++i)
            printf(" item%d(id=%u,state=%u)", i, GetMenuItemID(info.hMenu, i),
                GetMenuState(info.hMenu, i, MF_BYPOSITION));
    }
    putchar('\n');
    fflush(stdout);
}

bool SelectedItem(UINT command) {
    HWND popup = Popup();
    MENUBARINFO info{sizeof(info)};
    bool selected = false;
    if (popup && GetMenuBarInfo(popup, OBJID_CLIENT, 0, &info)) {
        for (int i = 0; i < GetMenuItemCount(info.hMenu); ++i)
            selected |= GetMenuItemID(info.hMenu, i) == command &&
                (GetMenuState(info.hMenu, i, MF_BYPOSITION) & MF_HILITE) != 0;
    }
    printf("expected_highlighted_command=%u\n", command);
    return Check("expected_menu_item_highlighted", selected);
}

bool FocusFirst() {
    SetForegroundWindow(firstWindow);
    Pump(120);
    return Check("owned_fixture_has_focus", GetForegroundWindow() == firstWindow);
}

bool Hotkey() {
    // Never send input if another application has taken the foreground.
    if (!Check("hotkey_input_target", GetForegroundWindow() == firstWindow &&
        IsWindow(application) && Owner(application) == applicationPid)) return false;
    const WORD keys[] = {VK_CONTROL, VK_MENU, 'B', 'B', VK_MENU, VK_CONTROL};
    INPUT inputs[ARRAYSIZE(keys)]{};
    for (unsigned i = 0; i < ARRAYSIZE(keys); ++i) {
        inputs[i].type = INPUT_KEYBOARD;
        inputs[i].ki.wVk = keys[i];
        if (i >= 3) inputs[i].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    const bool sent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT)) == ARRAYSIZE(inputs);
    Pump(650);
    return Check("real_ctrl_alt_b_sent", sent);
}

bool Matrix(const char* name, bool dark) {
    MAGCOLOREFFECT colors{};
    bool ok = MagGetFullscreenColorEffect(&colors) != FALSE;
    for (int row = 0; row < 5; ++row)
        for (int column = 0; column < 5; ++column)
            ok &= colors.transform[row][column] ==
                (dark && column < 3 ? 0.0f : originalColors.transform[row][column]);
    printf("MATRIX %s expected_dark=%d rgb=%.3f,%.3f,%.3f\n", name, dark,
        colors.transform[0][0], colors.transform[1][1], colors.transform[2][2]);
    if (!ok) MenuState("matrix_assertion_failed");
    return Check(name, ok);
}

bool OpenMenu() {
    if (!FocusFirst()) return false;
    POINT point{24, 42};
    ClientToScreen(firstWindow, &point);
    SetPhysicalCursorPos(point.x, point.y);
    AllowSetForegroundWindow(applicationPid);
    // Only our application's internal tray callback is triggered.
    PostMessageW(application, WM_APP + 1, 1, WM_RBUTTONUP);
    Pump(250);
    MenuState("opened");
    return Check("application_popup_open", Popup() != nullptr &&
        Owner(GetForegroundWindow()) == applicationPid);
}

bool MenuKey(WORD key) {
    if (!Check("menu_input_target", Popup() != nullptr &&
        Owner(GetForegroundWindow()) == applicationPid)) return false;
    INPUT inputs[2]{};
    inputs[0].type = inputs[1].type = INPUT_KEYBOARD;
    const UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    inputs[0].ki.wScan = inputs[1].ki.wScan = static_cast<WORD>(scan & 0xff);
    // Home/arrows use the E0 extended scan codes, not keypad navigation keys.
    inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE |
        ((scan & 0xff00) == 0xe000 ? KEYEVENTF_EXTENDEDKEY : 0);
    inputs[1].ki.dwFlags = inputs[0].ki.dwFlags | KEYEVENTF_KEYUP;
    const bool sent = SendInput(2, inputs, sizeof(INPUT)) == 2;
    Pump(200);
    MenuState("after_key", key);
    return Check("menu_key_sent", sent);
}

LRESULT CALLBACK FixtureProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        const wchar_t* label = L"Screen Save test window: input stays inside this fixture.";
        TextOutW(dc, 12, 12, label, static_cast<int>(wcslen(label)));
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || FindWindowW(L"ScreenSave", L"Screen Save")) {
        puts("Run with one candidate EXE path and no existing Screen Save instance.");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND originalForeground = GetForegroundWindow();
    POINT originalPosition;
    GetPhysicalCursorPos(&originalPosition);
    if (!MagInitialize()) return 2;
    if (!MagGetFullscreenColorEffect(&originalColors)) { MagUninitialize(); return 2; }

    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = FixtureProcedure;
    cls.lpszClassName = L"ScreenSaveMenuHotkeyFixture";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    firstWindow = CreateWindowW(cls.lpszClassName, L"Screen Save test A",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 520, 160,
        nullptr, nullptr, cls.hInstance, nullptr);
    secondWindow = CreateWindowW(cls.lpszClassName, L"Screen Save test B",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 640, 320, 520, 160,
        nullptr, nullptr, cls.hInstance, nullptr);

    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION child{};
    bool started = Check("candidate_created", CreateProcessW(argv[1], nullptr, nullptr,
        nullptr, FALSE, 0, nullptr, nullptr, &startup, &child) != FALSE);
    if (started) {
        applicationPid = child.dwProcessId;
        CloseHandle(child.hThread);
        AllowSetForegroundWindow(applicationPid);
        Pump(800);
        application = FindWindowW(L"ScreenSave", L"Screen Save");
        started = Check("candidate_window_owned", application != nullptr &&
            Owner(application) == applicationPid);
    }

    // Stop at the first failed setup/input guard; cleanup still runs below.
    if (started && FocusFirst()) {
        do {
            if (!Hotkey() || !Matrix("real_hotkey_matrix_on", true)) break;
            if (!Hotkey() || !Matrix("real_hotkey_matrix_off", false)) break;
            Check("hotkeys_keep_focus", GetForegroundWindow() == firstWindow);

            if (!OpenMenu() || !MenuKey(VK_ESCAPE)) break;
            if (!Check("escape_closes_menu", Popup() == nullptr)) break;
            Check("escape_restores_previous_focus", GetForegroundWindow() == firstWindow);

            // A mouse-opened menu starts with no selection. Down selects its first item.
            if (!OpenMenu() || !MenuKey(VK_DOWN) || !SelectedItem(1) ||
                !MenuKey(VK_RETURN)) break;
            Pump(450);
            if (!Matrix("keyboard_menu_toggles_matrix_on", true)) break;
            if (!Hotkey() || !Matrix("hotkey_restores_menu_toggle", false)) break;

            // Enter the mode submenu and select its first item (matrix).
            // This exercises native menu navigation without changing hardware brightness.
            if (!OpenMenu() || !MenuKey(VK_DOWN) || !MenuKey(VK_DOWN) ||
                !MenuKey(VK_RIGHT) || !SelectedItem(3) || !MenuKey(VK_RETURN)) break;
            if (!Check("keyboard_submenu_selection_closes_menu", Popup() == nullptr)) break;
            Matrix("submenu_selection_keeps_normal_output", false);

            if (!OpenMenu()) break;
            // Click only the uncovered client area of the second owned window.
            POINT click{36, 42};
            ClientToScreen(secondWindow, &click);
            if (!Check("cancel_click_target_owned", WindowFromPoint(click) == secondWindow)) break;
            SetPhysicalCursorPos(click.x, click.y);
            INPUT clickInputs[2]{};
            clickInputs[0].type = clickInputs[1].type = INPUT_MOUSE;
            clickInputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            clickInputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            bool clicked = SendInput(2, clickInputs, sizeof(INPUT)) == 2;
            Pump(450);
            Check("second_fixture_click_sent", clicked);
            Check("activation_cancels_menu", Popup() == nullptr);
            Check("cancel_does_not_steal_new_focus", GetForegroundWindow() == secondWindow);
        } while (false);
    }

    if (child.hProcess) {
        if (IsWindow(application) && Owner(application) == applicationPid) {
            PostMessageW(application, WM_CANCELMODE, 0, 0);
            Pump(150);
            PostMessageW(application, WM_CLOSE, 0, 0);
        }
        const ULONGLONG end = GetTickCount64() + 5000;
        while (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT &&
            GetTickCount64() < end) Pump(20);
        DWORD code = STILL_ACTIVE;
        GetExitCodeProcess(child.hProcess, &code);
        Check("candidate_normal_exit", code == 0);
        if (code == STILL_ACTIVE) puts("Candidate is still running; no forced termination was used.");
        CloseHandle(child.hProcess);
    }
    // Developer cleanup also restores output when an assertion failed mid-blackout.
    MagSetFullscreenColorEffect(&originalColors);
    MagShowSystemCursor(TRUE);
    DestroyWindow(secondWindow);
    DestroyWindow(firstWindow);
    SetPhysicalCursorPos(originalPosition.x, originalPosition.y);
    if (IsWindow(originalForeground)) SetForegroundWindow(originalForeground);
    MagUninitialize();
    printf("FAILURES=%d\n", failures);
    return failures ? 1 : 0;
}
