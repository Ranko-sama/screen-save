// Temporary, passive observer: no mouse injection, display setters or product edits.
#include <windows.h>
#include <cstdio>

LRESULT CALLBACK Mouse(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto& e = *reinterpret_cast<MSLLHOOKSTRUCT*>(data);
        printf("mouse,%lu,%llu,%ld,%ld,%lu\n", e.time,
            static_cast<unsigned long long>(message), e.pt.x, e.pt.y, e.flags);
        fflush(stdout);
    }
    return CallNextHookEx(nullptr, code, message, data);
}
LRESULT CALLBACK Keyboard(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto& e = *reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        if (e.vkCode == 'B' || e.vkCode == VK_LCONTROL || e.vkCode == VK_LMENU) {
            POINT p{};
            GetPhysicalCursorPos(&p);
            printf("key,%lu,%llu,%lu,%ld,%ld\n", e.time,
                static_cast<unsigned long long>(message), e.vkCode, p.x, p.y);
            fflush(stdout);
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}
int main() {
    POINT p{};
    GetPhysicalCursorPos(&p);
    printf("start,%llu,%ld,%ld\n", GetTickCount64(), p.x, p.y);
    fflush(stdout);
    HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, Mouse, GetModuleHandleW(nullptr), 0);
    HHOOK keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, Keyboard, GetModuleHandleW(nullptr), 0);
    if (!mouse || !keyboard) return 2;
    SetTimer(nullptr, 1, 180000, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_TIMER) break;
        DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(mouse);
    UnhookWindowsHookEx(keyboard);
    GetPhysicalCursorPos(&p);
    printf("end,%llu,%ld,%ld\n", GetTickCount64(), p.x, p.y);
    return 0;
}
