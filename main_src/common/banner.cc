// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/banner.h"

#include <iostream>

namespace prosophor {

void PrintBanner(const std::string& version) {
    std::cout << "\n";

    // Default robot face
    std::cout << "          ____        \n";
    std::cout << "     ╭─(─|    |─)-──╮\n";
    std::cout << "    ╭─────────────╮ |\n";
    std::cout << " )==│  ◉      ◉   │ |==( \n";
    std::cout << "    │     V       │ |\n";
    std::cout << "    ╰─────────────╯|\n";
    std::cout << "    ╰──────────────╯   \n";
    std::cout << "     ╰────╯  ╰────╯    \n";
    std::cout << "\n";

    std::cout << "Prosophor v" << version << "\n";
    std::cout << "AI Coding Assistant"  << "\n";
    std::cout << "\n";
    std::cout << "  Type 'exit' or Ctrl+D to quit, Ctrl+C to interrupt\n";
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

}  // namespace prosophor
