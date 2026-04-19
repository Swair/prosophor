#include "audior.h"
#include "sdl_common.h"
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

// AudiorImpl 定义
struct Audior::AudiorImpl {
    std::string file_path_{};
    MIX_Audio* sound_{nullptr};
    MIX_Track* mixer_track_{nullptr};
};

Audior::Audior(const std::string& file_path) : impl_(std::make_unique<AudiorImpl>()) {
    MIX_Mixer* mixer = SdlResource::Instance().GetMixer();
    impl_->mixer_track_= MIX_CreateTrack(mixer);
    impl_->file_path_ = file_path;
    impl_->sound_ = MIX_LoadAudio(mixer, file_path.c_str(), false);
    if (impl_->sound_ == nullptr) {
        LOG_ERROR("[Audior] Failed to load audio: {}", file_path.c_str());
    }

    MIX_SetTrackAudio(impl_->mixer_track_, impl_->sound_);

}

Audior::~Audior() {
    if(impl_->sound_ != nullptr) {
        MIX_DestroyAudio(impl_->sound_);
    }
}

bool Audior::Play(int loops) {
    // 创建属性来设置循环次数
    SDL_PropertiesID props = SDL_CreateProperties();
    if (loops >= 0) {
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
    } else {
        // -1 表示无限循环
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    }
    MIX_PlayTrack(impl_->mixer_track_, props);
    SDL_DestroyProperties(props);
    return true;
}
