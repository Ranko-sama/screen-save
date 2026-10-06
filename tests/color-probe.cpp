#include <windows.h>
#include <magnification.h>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    MAGCOLOREFFECT saved{};
    std::printf("Initialize=%d\n", MagInitialize());
    std::printf("Read=%d\n", MagGetFullscreenColorEffect(&saved));
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    const float gains[] = {argc > 1 ? static_cast<float>(std::atof(argv[1])) : 0.5f, 0.0f};
    const int stages = argc > 1 ? 1 : 2;
    const DWORD duration = argc > 2 ? std::strtoul(argv[2], nullptr, 10) * 1000 : 25000;
    for (int stage = 0; stage < stages; ++stage) {
        const float gain = gains[stage];
        auto effect = saved;
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                effect.transform[row][col] *= gain;
        std::printf("Gain=%.1f Applied=%d\n", gain, MagSetFullscreenColorEffect(&effect));
        std::fflush(stdout);
        Sleep(duration);
    }
    std::printf("Restored=%d\n", MagSetFullscreenColorEffect(&saved));
    SetThreadExecutionState(ES_CONTINUOUS);
    MagUninitialize();
}
