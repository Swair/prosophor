#ifndef ASSET_DEFINE_H
#define ASSET_DEFINE_H

#include <string>

#ifdef EMSCRIPTEN
const std::string ASSET_DIR{"/assets/"};
#else
const std::string ASSET_DIR{"assets/"};
#endif

const std::string IMAGE_DIR{ASSET_DIR + "image/"};
const std::string SOUND_DIR{ASSET_DIR + "sound/"};
const std::string MUSIC_DIR{ASSET_DIR + "music/"};
const std::string FONT_DIR{ASSET_DIR + "font/"};
const std::string EFFECT_DIR{ASSET_DIR + "effect/"};

struct AssetDefine {
    static AssetDefine& Instance() {
        static AssetDefine instance;
        return instance;
    }
    
    // Texture wizard_death{(ASSET_DIR + "xiaosuzaoshui/characters/Characters/Wizard/Wizard/Death.png")};

    // // Music
    // Audior music_battle_in_space{(MUSIC_DIR + "06_Battle_in_Space_Intro.ogg")};
    // Audior music_racing{(MUSIC_DIR + "03_Racing_Through_Asteroids_Loop.ogg")};

    // // Sound effects
    // Audior sound_laser_shoot{(SOUND_DIR + "laser_shoot4.wav")};
    // Audior sound_xs_laser{(SOUND_DIR + "xs_laser.wav")};
    // Audior sound_explosion1{(SOUND_DIR + "explosion1.wav")};
    // Audior sound_explosion3{(SOUND_DIR + "explosion3.wav")};
    // Audior sound_eff11{(SOUND_DIR + "eff11.wav")};
    // Audior sound_eff5{(SOUND_DIR + "eff5.wav")};
};

#endif
