#ifndef ASSET_DEFINE_H
#define ASSET_DEFINE_H

#include "prosophor_core/public/config.h"
#include <string>

inline std::string AssetBase() {
    return prosophor::ProsophorConfig::GetInstance().sprite_assets_dir + "/";
}

inline std::string PortraitDir() { return AssetBase() + "characters/portraits/"; }
inline std::string BackwallDir() { return AssetBase() + "backwalls/"; }
inline std::string ImageDir()   { return AssetBase() + "image/"; }
inline std::string SoundDir()   { return AssetBase() + "sound/"; }
inline std::string MusicDir()   { return AssetBase() + "music/"; }
inline std::string FontDir()    { return AssetBase() + "font/"; }
inline std::string EffectDir()  { return AssetBase() + "effect/"; }
#endif
