// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/banner.h"

#include <iostream>
#include "managers/buddy_manager.h"

namespace aicode {

void PrintBanner(const std::string& version, bool show_buddy) {
    std::cout << "\n";

    if (show_buddy) {
        // Print buddy sprite if available
        auto& buddy = BuddyManager::GetInstance();
        if (buddy.HasCompanion()) {
            auto sprite = buddy.RenderSprite(0);
            for (const auto& line : sprite) {
                std::cout << "  " << line << "\n";
            }
            std::cout << "\n";
        }
    }

    // Default robot face
    if (!show_buddy || !BuddyManager::GetInstance().HasCompanion()) {
        std::cout << "          ____        \n";
        std::cout << "     ╭─(─|    |─)-──╮\n";
        std::cout << "    ╭─────────────╮ |\n";
        std::cout << " A==│  ◉      ◉   │ |==C \n";
        std::cout << "    │     V       │ |\n";
        std::cout << "    ╰─────────────╯|\n";
        std::cout << "    ╰──────────────╯   \n";
        std::cout << "     ╰────╯  ╰────╯    \n";
        std::cout << "\n";
    }

    std::cout << "AiCode v" << version << "\n";
    std::cout << "AI Coding Assistant"  << "\n";
    std::cout << "\n";
    std::cout << "  Type 'exit' or Ctrl+D to quit, Ctrl+C to interrupt\n";
    std::cout << "  Type '/buddy' to see your companion pet\n";
    std::cout << "\n";
}

void PrintHelp() {
    std::cout << "\nCommands:\n";
    std::cout << "  /help     - Show this help message\n";
    std::cout << "  /clear    - Clear conversation history\n";
    std::cout << "  /model    - Show/change provider and model (overrides role config)\n";
    std::cout << "  /role     - Show/change current role\n";
    std::cout << "  /config   - Show current configuration\n";
    std::cout << "  /memory   - Show/save daily memory\n";
    std::cout << "  /exit     - Exit the application\n";
    std::cout << "\n";
}

}  // namespace aicode
