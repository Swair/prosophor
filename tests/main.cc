// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Initialize spdlog for tests
    spdlog::set_level(spdlog::level::warn);

    return RUN_ALL_TESTS();
}
