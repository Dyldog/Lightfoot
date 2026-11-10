// Device entry point. The toolkit owns the DRM screen and the panel's rawmidi;
// we supply the frame callback and run the codec output thread ourselves.
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "Game/Game.hpp"
#include "Game/GameBridge.hpp"
#include "Platform/Device/AlsaAudio.hpp"

extern "C" {
#include "app.h"
}

namespace {

// stderr goes to a log on the card so a failure can be read back without SSH.
void openLog() {
    const char* dir = std::getenv("MPC_APP_DIR");
    if (!dir) return;
    const std::string user = std::string(dir) + "/user";
    mkdir(user.c_str(), 0755);
    const std::string path = user + "/lightfoot.log";
    if (std::freopen(path.c_str(), "a", stderr)) {
        std::setvbuf(stderr, nullptr, _IOLBF, 0);
        const std::time_t now = std::time(nullptr);
        char stamp[64];
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::fprintf(stderr, "\n==== Lightfoot %s start %s ====\n", lightfoot::Game::version(), stamp);
    }
}

} // namespace

int main() {
    openLog();
    lightfoot::Game game;
    lightfoot::AlsaAudio audio(game.engine());
    audio.start();
    const int rc = app_run(lightfoot_frame, &game);
    audio.stop();
    std::fprintf(stderr, "lightfoot: exit %d (audio: %s)\n", rc, audio.status().c_str());
    return rc;
}
