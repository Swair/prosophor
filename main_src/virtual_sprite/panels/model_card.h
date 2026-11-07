#pragma once

#include <string>
#include "prosophor_core/public/config.h"
#include "media_engine/media_engine.h"

namespace prosophor {

/// Single model configuration card — renders model, temperature,
/// max_tokens, context_window fields inside a bordered child panel.
class ModelCard {
public:
    ModelCard(ModelConfig& config,
              size_t entry_idx,
              const std::string& model_key,
              float container_width,
              float sm);

    void Render();

private:
    std::string Id(const char* suf) const;

    ModelConfig& config_;
    size_t entry_idx_;
    std::string model_key_;
    float container_width_;
    float sm_;
};

} // namespace prosophor
