// Standalone check of the OpenXR path the plugin uses: it negotiates with the
// active runtime, opens a session and prints head poses. Nothing about the game
// is touched, so this can be run before ever launching EDF6.
#include "openxr_session.h"
#include "vr_math.h"
#include <cmath>
#include <cstdio>

static void Print(const char* text) { printf("[xr] %s\n", text); }

int main(int argc, char** argv) {
    int seconds = 5;
    if (argc > 1) seconds = atoi(argv[1]);
    if (seconds < 1) seconds = 1;
    if (seconds > 120) seconds = 120;
    if (!edf6vr::g_openxr.Start(&Print, edf6vr::XrMode::TrackingOnly, nullptr, nullptr)) {
        printf("FAILED: %s\n", edf6vr::g_openxr.Status());
        return 1;
    }
    printf("started: %s\n", edf6vr::g_openxr.Status());
    for (int i = 0; i < seconds * 4; ++i) {
        Sleep(250);
        edf6vr::HmdSample sample{};
        const bool have = edf6vr::g_openxr.Sample(sample);
        edf6vr::HeadBasis head{};
        const bool basis = have && edf6vr::HeadBasisFromXr(sample.orientation, head);
        printf("frames=%llu sample=%d pos=%d stage=%d room=(%.3f,%.3f,%.3f) "
               "gameYaw=%.4f gamePitchUp=%.4f up=(%.3f,%.3f,%.3f)\n",
               edf6vr::g_openxr.Frames(), have, sample.positionValid, edf6vr::g_openxr.StageSpace(),
               sample.position.x, sample.position.y, sample.position.z,
               basis ? head.yaw : 0.0f, basis ? head.pitchUp : 0.0f,
               head.up.x, head.up.y, head.up.z);
    }
    edf6vr::g_openxr.Stop();
    printf("stopped: %s\n", edf6vr::g_openxr.Status());
    return edf6vr::g_openxr.Frames() ? 0 : 2;
}
