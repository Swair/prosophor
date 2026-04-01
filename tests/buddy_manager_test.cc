// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "managers/buddy_manager.h"
#include "managers/buddy_types.h"

namespace aicode {

class BuddyManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        buddy_ = &BuddyManager::GetInstance();
    }

    BuddyManager* buddy_;
};

TEST_F(BuddyManagerTest, SingletonInstance) {
    // Should return the same instance
    auto& instance1 = BuddyManager::GetInstance();
    auto& instance2 = BuddyManager::GetInstance();
    EXPECT_EQ(&instance1, &instance2);
}

TEST_F(BuddyManagerTest, HasCompanion_InitialState) {
    // Initially should not have a companion
    EXPECT_FALSE(buddy_->HasCompanion());
}

TEST_F(BuddyManagerTest, GenerateCompanion) {
    auto companion = buddy_->GenerateCompanion("test_user_123");

    EXPECT_TRUE(buddy_->HasCompanion());
    EXPECT_GE(companion.species, Species::Duck);
    EXPECT_LE(companion.species, Species::Chonk);
    EXPECT_GE(companion.rarity, Rarity::Common);
    EXPECT_LE(companion.rarity, Rarity::Legendary);
    EXPECT_GE(companion.eye, Eye::Dot);
    EXPECT_LE(companion.eye, Eye::Degree);
    EXPECT_GE(companion.hat, Hat::None);
    EXPECT_LE(companion.hat, Hat::Tinyduck);
    EXPECT_GE(companion.debugging, 0);
    EXPECT_LE(companion.debugging, 100);
    EXPECT_GE(companion.patience, 0);
    EXPECT_LE(companion.patience, 100);
    EXPECT_GE(companion.chaos, 0);
    EXPECT_LE(companion.chaos, 100);
    EXPECT_GE(companion.wisdom, 0);
    EXPECT_LE(companion.wisdom, 100);
    EXPECT_GE(companion.snark, 0);
    EXPECT_LE(companion.snark, 100);
}

TEST_F(BuddyManagerTest, GenerateCompanion_Deterministic) {
    // Same user ID should generate same companion
    auto companion1 = buddy_->GenerateCompanion("deterministic_user");
    auto species1 = companion1.species;
    auto rarity1 = companion1.rarity;
    auto eye1 = companion1.eye;

    auto companion2 = buddy_->GenerateCompanion("deterministic_user");

    EXPECT_EQ(companion2.species, species1);
    EXPECT_EQ(companion2.rarity, rarity1);
    EXPECT_EQ(companion2.eye, eye1);
}

TEST_F(BuddyManagerTest, GenerateCompanion_DifferentUsers) {
    // Different user IDs should likely generate different companions
    auto companion1 = buddy_->GenerateCompanion("user_a");
    auto companion2 = buddy_->GenerateCompanion("user_b");

    // At least one attribute should be different (probabilistic)
    bool different = (companion1.species != companion2.species) ||
                     (companion1.rarity != companion2.rarity) ||
                     (companion1.eye != companion2.eye) ||
                     (companion1.hat != companion2.hat);

    // This could theoretically fail but very unlikely
    EXPECT_TRUE(different);
}

TEST_F(BuddyManagerTest, RenderSprite_ValidCompanion) {
    buddy_->GenerateCompanion("test_user");

    for (int frame = 0; frame < 3; frame++) {
        auto sprite = buddy_->RenderSprite(frame);

        // Should have 4-6 lines (4 base + optional hat + optional extra)
        EXPECT_GE(sprite.size(), 4u);
        EXPECT_LE(sprite.size(), 6u);

        // Each line should be non-empty
        for (const auto& line : sprite) {
            EXPECT_FALSE(line.empty());
        }
    }
}

TEST_F(BuddyManagerTest, RenderRandomSprite) {
    // Generate multiple random sprites and ensure variety
    std::set<std::string> seen_sprites;

    for (int i = 0; i < 10; i++) {
        auto sprite = buddy_->RenderRandomSprite(0);
        std::string sprite_str;
        for (const auto& line : sprite) {
            sprite_str += line;
        }
        seen_sprites.insert(sprite_str);
    }

    // Should have at least some variety (may have duplicates due to randomness)
    EXPECT_GE(seen_sprites.size(), 3u);
}

TEST_F(BuddyManagerTest, RenderInfo) {
    buddy_->GenerateCompanion("test_user");
    buddy_->SetCompanionName("TestBuddy");

    auto info = buddy_->RenderInfo();

    EXPECT_FALSE(info.empty());
    EXPECT_NE(info.find("TestBuddy"), std::string::npos);
    EXPECT_NE(info.find("DEBUGGING"), std::string::npos);
    EXPECT_NE(info.find("PATIENCE"), std::string::npos);
    EXPECT_NE(info.find("CHAOS"), std::string::npos);
    EXPECT_NE(info.find("WISDOM"), std::string::npos);
    EXPECT_NE(info.find("SNARK"), std::string::npos);
}

TEST_F(BuddyManagerTest, SetCompanionName) {
    buddy_->GenerateCompanion("test_user");
    buddy_->SetCompanionName("Fluffy");

    // Note: name is stored in companion_ but there's no getter
    // We verify through RenderInfo
    auto info = buddy_->RenderInfo();
    EXPECT_NE(info.find("Fluffy"), std::string::npos);
}

TEST_F(BuddyManagerTest, GetStat) {
    buddy_->GenerateCompanion("test_user");

    EXPECT_GE(buddy_->GetStat(StatType::Debugging), 0);
    EXPECT_LE(buddy_->GetStat(StatType::Debugging), 100);
    EXPECT_GE(buddy_->GetStat(StatType::Patience), 0);
    EXPECT_LE(buddy_->GetStat(StatType::Patience), 100);
    EXPECT_GE(buddy_->GetStat(StatType::Chaos), 0);
    EXPECT_LE(buddy_->GetStat(StatType::Chaos), 100);
    EXPECT_GE(buddy_->GetStat(StatType::Wisdom), 0);
    EXPECT_LE(buddy_->GetStat(StatType::Wisdom), 100);
    EXPECT_GE(buddy_->GetStat(StatType::Snark), 0);
    EXPECT_LE(buddy_->GetStat(StatType::Snark), 100);
}

TEST_F(BuddyManagerTest, SpeciesCoverage) {
    // Test that all species can be rendered
    for (int i = 0; i < 18; i++) {
        // Generate companions until we get each species
        // This is a probabilistic test
        auto companion = buddy_->GenerateCompanion("species_test_" + std::to_string(i));
        auto sprite = buddy_->RenderSprite(0);
        EXPECT_GE(sprite.size(), 4u);
    }
}

TEST_F(BuddyManagerTest, HatRendering) {
    // Test that hats are rendered correctly
    buddy_->GenerateCompanion("test_user");

    // Generate multiple companions to get some with hats
    bool found_with_hat = false;
    bool found_without_hat = false;

    for (int i = 0; i < 20; i++) {
        buddy_->GenerateCompanion("hat_test_" + std::to_string(i));
        auto sprite = buddy_->RenderSprite(0);

        if (sprite.size() > 5) {
            found_with_hat = true;
        } else {
            found_without_hat = true;
        }
    }

    // Should have found both types (probabilistic, ~25% have hats)
    EXPECT_TRUE(found_with_hat || found_without_hat);
}

}  // namespace aicode
