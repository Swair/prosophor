#pragma once

#include <string>
#include <unordered_map>
#include "config/config.h"
#include "media_engine/media_engine.h"

namespace prosophor {

class ProviderEntryCard {
public:
    ProviderEntryCard(LlmProviderEntryConfig& entry,
                      size_t entry_idx,
                      float width,
                      float sm);

    void Render();

private:
    std::string Id(const char* suf) const;
    void RenderField(const char* label,
                     float entry_fy, float entry_cx, float widget_w, float entry_lh,
                     std::function<void(float)> widget_fn,
                     float& next_fy);

    LlmProviderEntryConfig& entry_;
    size_t entry_idx_;
    float width_;
    float sm_;

    static std::unordered_map<size_t, bool> s_models_open;
};

} // namespace prosophor
