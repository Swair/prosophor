// NutriBotApp::Run —— 常驻语音/感知回路入口。
#include "entry/nutribot_app.h"

#include <string>

#include "common/log_wrapper.h"
#include "entry/resident_session.h"

namespace prosophor::nr::nutribot {

int NutriBotApp::Run(int /*argc*/, char** /*argv*/) {
    ResidentSession session;
    std::string error;
    if (!session.Init(error)) {
        LOG_ERROR("nutribot: {}", error.c_str());
        return 1;
    }
    return session.Run();
}

}  // namespace prosophor::nr::nutribot
