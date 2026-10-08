// Developer-only regression of the actual product source against in-memory APIs.
// No window is created. Display, cursor, power, WMI writes and quit are replaced.
#include <windows.h>
#include <magnification.h>
#include <wmistr.h>
#include <cstdio>
#include <cstring>
#include <vector>

BOOL WINAPI ModelGetColors(PMAGCOLOREFFECT);
BOOL WINAPI ModelSetColors(PMAGCOLOREFFECT);
BOOL WINAPI ModelShowCursor(BOOL);
BOOL WINAPI ModelGetGamma(HDC, LPVOID);
BOOL WINAPI ModelSetGamma(HDC, LPVOID);
BOOL WINAPI ModelGetCursor(LPPOINT);
BOOL WINAPI ModelSetCursor(int, int);
UINT WINAPI ModelSendInput(UINT, LPINPUT, int);
EXECUTION_STATE WINAPI ModelExecution(EXECUTION_STATE);
void WINAPI ModelQuit(int);
LRESULT WINAPI ModelDefault(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI ModelMessage(HWND, UINT, WPARAM, LPARAM);
#define MagGetFullscreenColorEffect ModelGetColors
#define MagSetFullscreenColorEffect ModelSetColors
#define MagShowSystemCursor ModelShowCursor
#define GetDeviceGammaRamp ModelGetGamma
#define SetDeviceGammaRamp ModelSetGamma
#define GetPhysicalCursorPos ModelGetCursor
#define SetPhysicalCursorPos ModelSetCursor
#define SendInput ModelSendInput
#define SetThreadExecutionState ModelExecution
#define PostQuitMessage ModelQuit
#define DefWindowProcW ModelDefault
#define SendMessageW ModelMessage
#define wWinMain UnusedProductEntry
#include SCREEN_SAVE_SOURCE

struct ModelState {
    MAGCOLOREFFECT colors;
    WORD gamma[3][256];
    int brightness = 73;
    bool visible = true;
    POINT cursor{300, 400};
    EXECUTION_STATE execution = ES_CONTINUOUS;
    int reads = 0, writes = 0, quits = 0, absoluteWrites = 0;
    bool concurrentMove = false, moved = false, handoff = false;
    std::vector<char> effects;
} model;
MAGCOLOREFFECT savedColors;
WORD savedGamma[3][256];
int savedBrightness;
const POINT movedCursor{700, 800};
int checks, failures;
void Check(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; printf("FAIL %s\n", name); }
}
bool SameColors() { return !memcmp(&model.colors, &savedColors, sizeof(savedColors)); }
bool SameGamma() { return !memcmp(model.gamma, savedGamma, sizeof(savedGamma)); }
void Effect(char kind) {
    ++model.writes;
    model.effects.push_back(kind);
    if (model.handoff && (kind == 'C' || kind == 'G' || kind == 'B'))
        Check(!(SameColors() && SameGamma()), "handoff_never_removes_both_color_effects");
}
BOOL WINAPI ModelGetColors(PMAGCOLOREFFECT value) { ++model.reads; *value = model.colors; return TRUE; }
BOOL WINAPI ModelSetColors(PMAGCOLOREFFECT value) { model.colors = *value; Effect('C'); return TRUE; }
BOOL WINAPI ModelShowCursor(BOOL show) { model.visible = show != FALSE; Effect('V'); return TRUE; }
BOOL WINAPI ModelGetGamma(HDC, LPVOID value) { ++model.reads; memcpy(value, model.gamma, sizeof(model.gamma)); return TRUE; }
BOOL WINAPI ModelSetGamma(HDC, LPVOID value) { memcpy(model.gamma, value, sizeof(model.gamma)); Effect('G'); return TRUE; }
void MoveConcurrently() { model.cursor = movedCursor; model.moved = true; }
BOOL WINAPI ModelGetCursor(LPPOINT value) {
    *value = model.cursor;
    // Force the actual read/write interleaving, without moving the real mouse.
    if (model.concurrentMove) MoveConcurrently();
    return TRUE;
}
BOOL WINAPI ModelSetCursor(int x, int y) {
    ++model.absoluteWrites;
    model.cursor = {x, y};
    Effect('R');
    return TRUE;
}
UINT WINAPI ModelSendInput(UINT count, LPINPUT input, int bytes) {
    Check(count == 1 && bytes == sizeof(INPUT), "one_mouse_packet");
    Check(input->type == INPUT_MOUSE && input->mi.dx == 0 && input->mi.dy == 0 &&
        input->mi.mouseData == 0 && input->mi.dwFlags == (MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE),
        "relative_zero_move_only");
    if (model.concurrentMove) MoveConcurrently();
    model.cursor.x += input->mi.dx;
    model.cursor.y += input->mi.dy;
    Effect('R');
    return count;
}
EXECUTION_STATE WINAPI ModelExecution(EXECUTION_STATE value) { model.execution = value; Effect('P'); return ES_CONTINUOUS; }
void WINAPI ModelQuit(int) { ++model.quits; }
LRESULT WINAPI ModelDefault(HWND, UINT message, WPARAM, LPARAM) { return message == WM_QUERYENDSESSION ? TRUE : 0; }
LRESULT WINAPI ModelMessage(HWND window, UINT message, WPARAM w, LPARAM l) { return WindowProcedure(window, message, w, l); }
ULONG WINAPI ModelReadBrightness(HANDLE, ULONG* bytes, void* data) {
    ++model.reads;
    Check(*bytes >= sizeof(WNODE_ALL_DATA) + 1, "brightness_query_buffer");
    auto node = static_cast<WNODE_ALL_DATA*>(data);
    node->DataBlockOffset = sizeof(WNODE_ALL_DATA);
    static_cast<BYTE*>(data)[node->DataBlockOffset] = static_cast<BYTE>(model.brightness);
    return 0;
}
ULONG WINAPI ModelWriteBrightness(HANDLE, const wchar_t*, ULONG method, ULONG bytes,
    void* data, ULONG* resultBytes, void* result) {
    Check(method == 1 && bytes == 5 && *resultBytes == sizeof(ULONG), "brightness_method_contract");
    model.brightness = static_cast<BYTE*>(data)[4];
    *static_cast<ULONG*>(result) = 0;
    Effect('B');
    return 0;
}
void Reset(DimmingMode mode) {
    model = ModelState{};
    for (int row = 0; row < 5; ++row)
        for (int column = 0; column < 5; ++column)
            model.colors.transform[row][column] = (row == column ? 1.0f : 0.0f) + (row * 5 + column + 1) / 1024.0f;
    for (int channel = 0; channel < 3; ++channel)
        for (int entry = 0; entry < 256; ++entry)
            model.gamma[channel][entry] = static_cast<WORD>(entry * entry * (channel + 3) / 5);
    savedColors = model.colors;
    savedBrightness = model.brightness;
    memcpy(savedGamma, model.gamma, sizeof(savedGamma));
    memset(&originalColors, 0, sizeof(originalColors));
    memset(originalGamma, 0, sizeof(originalGamma));
    dimmingMode = mode;
    isDark = false;
    brightnessBeforeDark = -1;
    nativeQuery = ModelReadBrightness;
    nativeExecute = ModelWriteBrightness;
}
void Restored(const char* name) {
    Check(!isDark && SameColors() && SameGamma() && model.brightness == savedBrightness &&
        model.visible && model.execution == ES_CONTINUOUS, name);
}
void Dark(DimmingMode mode) {
    bool exact = true;
    for (int row = 0; row < 5; ++row)
        for (int column = 0; column < 5; ++column) {
            float expected = mode == DimmingMode::Matrix && column < 3 ? 0.0f : savedColors.transform[row][column];
            exact = exact && model.colors.transform[row][column] == expected;
        }
    for (int channel = 0; channel < 3; ++channel)
        for (int entry = 0; entry < 256; ++entry) {
            WORD expected = mode == DimmingMode::Gamma ? savedGamma[channel][entry] / 2 : savedGamma[channel][entry];
            exact = exact && model.gamma[channel][entry] == expected;
        }
    Check(exact && isDark && model.brightness == (mode == DimmingMode::Gamma ? 0 : savedBrightness) &&
        model.visible == (mode == DimmingMode::Gamma) &&
        model.execution == (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED), "dark_full_state");
}
void Notify(UINT message, WPARAM w = 0, LPARAM l = 0) { WindowProcedure(nullptr, message, w, l); }
void SessionCases(DimmingMode mode) {
    for (bool enterDark : {false, true}) {
        for (LPARAM reason : {LPARAM(0), LPARAM(ENDSESSION_LOGOFF)}) {
            Reset(mode);
            if (enterDark) { Notify(WM_HOTKEY, 3); Dark(mode); }
            int reads = model.reads, writes = model.writes;
            Check(WindowProcedure(nullptr, WM_QUERYENDSESSION, 0, reason) == TRUE, "query_allows_normal_shutdown");
            Notify(WM_ENDSESSION, FALSE, reason);
            Check(model.reads == reads && model.writes == writes && model.quits == 0 && isDark == enterDark,
                "cancelled_session_changes_nothing");
            Notify(WM_ENDSESSION, TRUE, reason);
            Restored("confirmed_session_restores_before_return");
            Check(model.reads == reads, "restore_reuses_session_snapshot");
            if (!enterDark) Check(model.writes == writes, "normal_session_does_not_write_display");
        }
    }
}
void InteractionCases() {
    for (DimmingMode mode : {DimmingMode::Matrix, DimmingMode::Gamma}) {
        Reset(mode);
        auto other = mode == DimmingMode::Matrix ? DimmingMode::Gamma : DimmingMode::Matrix;
        SelectMode(other);
        Check(model.reads == 0 && model.writes == 0, "normal_selection_has_no_display_work");
        SelectMode(mode);
        Notify(WM_HOTKEY, 3);
        Dark(mode);
        int reads = model.reads;
        Notify(WM_ENDSESSION, FALSE, ENDSESSION_LOGOFF);
        model.handoff = true;
        SelectMode(other);
        Dark(other);
        SelectMode(mode);
        Dark(mode);
        model.handoff = false;
        Check(model.reads == reads, "handoffs_keep_original_snapshot");
        Notify(WM_HOTKEY, 3);
        Restored("toggle_restores_after_cancel_and_handoffs");
        model.brightness = 41;
        savedBrightness = model.brightness;
        model.colors.transform[0][0] += 0.25f;
        savedColors = model.colors;
        model.gamma[2][255] -= 2;
        memcpy(savedGamma, model.gamma, sizeof(savedGamma));
        Notify(WM_HOTKEY, 3);
        Notify(WM_ENDSESSION, TRUE);
        Restored("next_session_uses_fresh_external_snapshot");
        int writes = model.writes;
        int quits = model.quits;
        Notify(WM_CLOSE);
        Check(model.quits == quits + 1 && model.writes == writes, "normal_close_keeps_existing_quit_path");
    }
}
void CursorCase() {
    Reset(DimmingMode::Matrix);
    Notify(WM_HOTKEY, 3);
    model.concurrentMove = true;
    Notify(WM_HOTKEY, 3);
    // An implementation without a position-dependent API also preserves a move.
    if (!model.moved) MoveConcurrently();
    Check(model.cursor.x == movedCursor.x && model.cursor.y == movedCursor.y,
        "concurrent_move_is_not_rolled_back");
}
int main() {
    SessionCases(DimmingMode::Matrix);
    SessionCases(DimmingMode::Gamma);
    InteractionCases();
    CursorCase();
    printf("CHECKS=%d FAILURES=%d\n", checks, failures);
    return failures ? 1 : 0;
}
