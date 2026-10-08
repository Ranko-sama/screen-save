// Developer-only DXGI hardware-pointer observation. No frames are saved.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <magnification.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;

ComPtr<IDXGIOutputDuplication> duplication;
int visible = -1;
int captureErrors;

int Sample(const char* name) {
    int updates = 0;
    const auto end = GetTickCount64() + 600;
    while (GetTickCount64() < end) {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> frame;
        HRESULT hr = duplication->AcquireNextFrame(100, &info, &frame);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (FAILED(hr)) { printf("frame_error=%08lx\n", hr); ++captureErrors; break; }
        if (info.LastMouseUpdateTime.QuadPart) {
            visible = info.PointerPosition.Visible;
            ++updates;
        }
        duplication->ReleaseFrame();
    }
    POINT p;
    GetPhysicalCursorPos(&p);
    printf("%s hardware_pointer_visible=%d updates=%d position=%ld,%ld\n",
        name, visible, updates, p.x, p.y);
    fflush(stdout);
    return updates;
}

int main(int argc, char**) {
    ComPtr<ID3D11Device> device;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr);
    if (FAILED(hr)) { printf("device_error=%08lx\n", hr); return 1; }
    ComPtr<IDXGIDevice> dxgi;
    device.As(&dxgi);
    ComPtr<IDXGIAdapter> adapter;
    dxgi->GetAdapter(&adapter);
    ComPtr<IDXGIOutput> output;
    adapter->EnumOutputs(0, &output);
    ComPtr<IDXGIOutput1> output1;
    output.As(&output1);
    hr = output1->DuplicateOutput(device.Get(), &duplication);
    if (FAILED(hr)) { printf("duplication_error=%08lx\n", hr); return 1; }
    if (argc > 1) {
        HWND app = FindWindowW(L"ScreenSave", L"Screen Save");
        if (!app) return 2;
        struct Step { const char* name; UINT message; WPARAM command; bool restores; };
        const Step steps[] = {
            {"matrix_on_1", WM_HOTKEY, 3, false},
            {"matrix_off_1", WM_HOTKEY, 3, true},
            {"matrix_on_2", WM_HOTKEY, 3, false},
            {"matrix_off_2", WM_HOTKEY, 3, true},
            {"matrix_on_3", WM_HOTKEY, 3, false},
            {"matrix_off_3", WM_HOTKEY, 3, true},
            {"matrix_on_for_switch", WM_HOTKEY, 3, false},
            {"matrix_to_gamma", WM_COMMAND, 4, true},
            {"gamma_to_matrix", WM_COMMAND, 3, false},
            {"exit_from_matrix", WM_CLOSE, 0, true}
        };
        int failures = 0;
        Sample("application_baseline");
        for (const auto& step : steps) {
            POINT before, after;
            GetPhysicalCursorPos(&before);
            HWND foreground = GetForegroundWindow();
            PostMessageW(app, step.message, step.command, 0);
            int updates = Sample(step.name);
            GetPhysicalCursorPos(&after);
            bool samePosition = before.x == after.x && before.y == after.y;
            bool sameForeground = foreground == GetForegroundWindow();
            bool ok = samePosition && sameForeground && !captureErrors &&
                visible == (step.restores ? TRUE : FALSE) &&
                (!step.restores || updates > 0);
            printf("%s %s position_unchanged=%d foreground_unchanged=%d\n",
                ok ? "PASS" : "FAIL", step.name, samePosition, sameForeground);
            if (!ok) ++failures;
        }
        printf("FAILURES=%d\n", failures);
        return failures ? 1 : 0;
    }
    MagInitialize();
    MAGCOLOREFFECT original;
    MagGetFullscreenColorEffect(&original);
    Sample("baseline");
    auto black = original;
    for (int r=0;r<5;++r)
        for (int c=0;c<3;++c) black.transform[r][c]=0;
    MagSetFullscreenColorEffect(&black);
    MagShowSystemCursor(FALSE);
    Sample("black_hidden");
    MagSetFullscreenColorEffect(&original);
    MagShowSystemCursor(TRUE);
    Sample("ordinary_restore");
    POINT p;
    GetPhysicalCursorPos(&p);
    printf("same_position_result=%d\n", SetPhysicalCursorPos(p.x,p.y));
    Sample("same_position");
    printf("dwm_flush_result=%08lx\n", DwmFlush());
    Sample("dwm_flush");
    printf("uninitialize_result=%d\n", MagUninitialize());
    Sample("uninitialize");
}
