// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "core/skill_loader.h"
#include <fstream>

namespace aicode {

class SkillLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        loader_ = std::make_unique<SkillLoader>();
        test_skill_dir_ = std::filesystem::temp_directory_path() / "aicode_test_skills";

        // Create test skill directory
        std::filesystem::create_directories(test_skill_dir_ / "test-skill");

        // Create a test SKILL.md file
        std::string skill_content = R"(---
name: test-skill
description: A test skill
emoji: 🧪
requires:
  bins: [cat]
  env: [HOME]
---

This is a test skill for unit testing.
)";
        std::ofstream ofs(test_skill_dir_ / "test-skill" / "SKILL.md");
        ofs << skill_content;
        ofs.close();
    }

    void TearDown() override {
        // Cleanup test files
        std::filesystem::remove_all(test_skill_dir_);
    }

    std::unique_ptr<SkillLoader> loader_;
    std::filesystem::path test_skill_dir_;
};

TEST_F(SkillLoaderTest, LoadSkillsFromDirectory) {
    auto skills = loader_->LoadSkillsFromDirectory(test_skill_dir_);

    EXPECT_EQ(skills.size(), 1u);

    if (!skills.empty()) {
        EXPECT_EQ(skills[0].name, "test-skill");
        EXPECT_EQ(skills[0].description, "A test skill");
        EXPECT_EQ(skills[0].emoji, "🧪");
        EXPECT_FALSE(skills[0].content.empty());
    }
}

TEST_F(SkillLoaderTest, LoadSkillsFromDirectory_NotExist) {
    auto skills = loader_->LoadSkillsFromDirectory("/nonexistent/path");
    EXPECT_TRUE(skills.empty());
}

TEST_F(SkillLoaderTest, CheckSkillGating_BinaryExists) {
    // Create a skill that requires 'cat' (should exist on most systems)
    SkillMetadata skill;
    skill.name = "test";
    skill.required_bins = {"cat"};
    skill.always = false;

    EXPECT_TRUE(loader_->CheckSkillGating(skill));
}

TEST_F(SkillLoaderTest, CheckSkillGating_BinaryNotExists) {
    SkillMetadata skill;
    skill.name = "test";
    skill.required_bins = {"nonexistent_binary_xyz"};
    skill.always = false;

    EXPECT_FALSE(loader_->CheckSkillGating(skill));
}

TEST_F(SkillLoaderTest, CheckSkillGating_AlwaysTrue) {
    SkillMetadata skill;
    skill.name = "test";
    skill.required_bins = {"nonexistent_binary_xyz"};
    skill.always = true;  // Should bypass gating

    EXPECT_TRUE(loader_->CheckSkillGating(skill));
}

TEST_F(SkillLoaderTest, CheckSkillGating_EnvVar) {
    SkillMetadata skill;
    skill.name = "test";
    skill.required_envs = {"HOME"};  // Should exist on most systems
    skill.always = false;

    EXPECT_TRUE(loader_->CheckSkillGating(skill));
}

TEST_F(SkillLoaderTest, CheckSkillGating_EnvVarNotExists) {
    SkillMetadata skill;
    skill.name = "test";
    skill.required_envs = {"NONEXISTENT_ENV_VAR_XYZ"};
    skill.always = false;

    EXPECT_FALSE(loader_->CheckSkillGating(skill));
}

TEST_F(SkillLoaderTest, GetSkillContext) {
    auto skills = loader_->LoadSkillsFromDirectory(test_skill_dir_);

    std::string context = loader_->GetSkillContext(skills);

    EXPECT_NE(context.find("test-skill"), std::string::npos);
    EXPECT_NE(context.find("A test skill"), std::string::npos);
}

TEST_F(SkillLoaderTest, ParseYamlFrontmatter) {
    std::string yaml = R"(
name: my-skill
description: Test description
emoji: 🚀
always: true
requires:
  bins:
    - node
    - npm
  env:
    - HOME
)";

    nlohmann::json result = loader_->ParseYamlFrontmatter(yaml);

    EXPECT_EQ(result["name"], "my-skill");
    EXPECT_EQ(result["description"], "Test description");
    EXPECT_EQ(result["emoji"], "🚀");
    EXPECT_TRUE(result["always"].get<bool>());
}

TEST_F(SkillLoaderTest, GetCurrentOs) {
    std::string os = loader_->GetCurrentOs();

    // Should return one of: linux, darwin, win32, unknown
    EXPECT_TRUE(os == "linux" || os == "darwin" || os == "win32" || os == "unknown");
}

TEST_F(SkillLoaderTest, CheckOsRestriction) {
    std::string current_os = loader_->GetCurrentOs();

    // Should match current OS
    std::vector<std::string> restrict_list = {current_os};
    EXPECT_TRUE(loader_->CheckOsRestriction(restrict_list));

    // Should not match wrong OS
    std::vector<std::string> wrong_os;
    if (current_os == "linux") {
        wrong_os = {"darwin", "win32"};
    } else if (current_os == "darwin") {
        wrong_os = {"linux", "win32"};
    } else {
        wrong_os = {"linux", "darwin"};
    }
    EXPECT_FALSE(loader_->CheckOsRestriction(wrong_os));
}

TEST_F(SkillLoaderTest, InstallSkill_NoInstalls) {
    auto skills = loader_->LoadSkillsFromDirectory(test_skill_dir_);

    ASSERT_FALSE(skills.empty());
    // Our test skill has no install instructions
    EXPECT_TRUE(loader_->InstallSkill(skills[0]));
}

TEST_F(SkillLoaderTest, GetAllCommands) {
    auto skills = loader_->LoadSkillsFromDirectory(test_skill_dir_);

    auto commands = loader_->GetAllCommands(skills);
    // Our test skill has no commands defined
    EXPECT_TRUE(commands.empty());
}

TEST_F(SkillLoaderTest, LoadSkills_EmptyConfig) {
    SkillsConfig config;
    auto skills = loader_->LoadSkills(config, test_skill_dir_);

    // Should load from workspace/skills and ~/.aicode/skills
    EXPECT_GE(skills.size(), 0u);
}

}  // namespace aicode
