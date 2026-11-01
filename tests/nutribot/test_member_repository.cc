// JsonMemberRepository 测试：临时目录 CRUD / 序列化
#include <filesystem>
#include <string>

#include "domain/member.h"
#include "storage/member_repository.h"
#include "micro_test.hpp"

using namespace prosophor::nr::infra;
using prosophor::nr::domain::Member;

namespace {

std::string make_tmp_dir() {
    const std::string d =
        "/tmp/nr_repo_test_" + std::to_string(std::rand());
    std::filesystem::create_directories(d);
    return d;
}

Member make_zh_member(const std::string& name) {
    Member m;
    m.member_id = "id-" + name;
    m.name = name;
    m.profile.age = 6;
    m.profile.gender = "男";
    m.profile.health_tags = {"儿童"};
    m.profile.taboo_foods = {"牛奶", "花生"};
    m.profile.allergies = {"虾"};
    m.profile.eating_habits = {"少盐"};
    m.profile.preferences = {"清淡"};
    m.profile.dri_overrides = {{"energy_kcal", 1800.0}};
    m.registered_at = "2026-09-08 10:00:00";
    return m;
}

}  // namespace

TEST(repo, add_get_list_delete_roundtrip) {
    const std::string dir = make_tmp_dir();
    JsonMemberRepository repo(dir);

    Member m = make_zh_member("小明");
    repo.add_member(m);

    // get
    const auto got = repo.get_member("小明");
    EXPECT_TRUE(got.has_value());
    EXPECT_EQ(got->name, "小明");
    EXPECT_EQ(got->member_id, "id-小明");
    EXPECT_TRUE(got->profile.age.has_value() && *got->profile.age == 6);
    EXPECT_EQ(got->profile.taboo_foods.size(), 2u);   // 全字段保留
    EXPECT_EQ(got->profile.dri_overrides.at("energy_kcal"), 1800.0);
    EXPECT_EQ(got->profile.eating_habits[0], "少盐");

    // list
    EXPECT_EQ(repo.list_members().size(), 1u);

    // 缺失 id/时间自动补齐
    Member m2 = make_zh_member("小红");
    m2.member_id.clear();
    m2.registered_at.clear();
    repo.add_member(m2);
    const auto got2 = repo.get_member("小红");
    EXPECT_TRUE(got2.has_value());
    EXPECT_FALSE(got2->member_id.empty());
    EXPECT_FALSE(got2->registered_at.empty());

    // delete
    repo.delete_member("小明");
    EXPECT_FALSE(repo.get_member("小明").has_value());
    EXPECT_EQ(repo.list_members().size(), 1u);
    std::filesystem::remove_all(dir);
}

TEST(repo, get_missing_returns_nullopt) {
    const std::string dir = make_tmp_dir();
    JsonMemberRepository repo(dir);
    EXPECT_FALSE(repo.get_member("不存在").has_value());
    std::filesystem::remove_all(dir);
}

TEST(repo, member_json_serialize_roundtrip) {
    Member m = make_zh_member("序列化");
    const std::string text = JsonMemberRepository::member_to_json(m);
    // 中文 ensure 可读（nlohmann dump 默认转义 \uXXXX 亦可解析）
    const Member back = JsonMemberRepository::member_from_json(text);
    EXPECT_EQ(back.name, "序列化");
    EXPECT_EQ(back.profile.taboo_foods.size(), 2u);
    EXPECT_EQ(back.profile.gender, "男");
}
