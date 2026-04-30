// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace prosophor {

/// Print the application banner with ASCII art
void PrintBanner(const std::string& version);

/// Print help message for available commands
void PrintHelp();

}  // namespace prosophor
