// vision_stub 单测：常在场桩的命中/未命中/档案动态生效（内存 Fake 仓储，零 I/O）
#include "perception/vision_stub.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/member.h"
#include "micro_test.hpp"

namespace {

using prosophor::nr::app::StubFaceRecognitionPort;
using prosophor::nr::domain::Member;
using prosophor::nr::domain::MemberMatch;
using prosophor::nr::domain::MemberRepository;

class FakeRepository final : public MemberRepository {
public:
    std::optional<MemberMatch> find_match(const std::vector<float>&, double) override {
        return std::nullopt;
    }
    std::vector<Member> list_members() override {
        std::vector<Member> out;
        for (const auto& [name, member] : members_) out.push_back(member);
        return out;
    }
    std::optional<Member> get_member(const std::string& name) override {
        const auto it = members_.find(name);
        if (it == members_.end()) return std::nullopt;
        return it->second;
    }
    void add_member(const Member& member) override { members_[member.name] = member; }
    void delete_member(const std::string& name) override { members_.erase(name); }

private:
    std::unordered_map<std::string, Member> members_;
};

Member MakeChild() {
    Member member;
    member.member_id = "m1";
    member.name = "小明";
    member.profile.age = 6;
    member.profile.health_tags = {"儿童", "乳糖不耐受"};
    member.profile.taboo_foods = {"牛奶", "花生"};
    member.profile.allergies = {"虾"};
    return member;
}

}  // namespace

TEST(vision_stub, reports_registered_member_as_present) {
    FakeRepository repo;
    repo.add_member(MakeChild());
    StubFaceRecognitionPort stub("小明");

    const auto observations = stub.detect_and_recognize(nullptr, repo, 0.5, 40);
    EXPECT_EQ(observations.size(), 1u);
    EXPECT_EQ(observations[0].name.value(), std::string("小明"));
    EXPECT_TRUE(observations[0].known());
    EXPECT_EQ(observations[0].similarity, 1.0);
    EXPECT_EQ(observations[0].health_tags.size(), 2u);
    EXPECT_EQ(observations[0].taboo_foods.size(), 2u);
}

TEST(vision_stub, returns_empty_when_member_not_registered) {
    FakeRepository repo;   // 空库
    StubFaceRecognitionPort stub("小明");
    EXPECT_TRUE(stub.detect_and_recognize(nullptr, repo, 0.5, 40).empty());
}

TEST(vision_stub, profile_edits_visible_without_restart) {
    FakeRepository repo;
    repo.add_member(MakeChild());
    StubFaceRecognitionPort stub("小明");
    EXPECT_EQ(stub.detect_and_recognize(nullptr, repo, 0.5, 40)[0].taboo_foods.size(), 2u);

    Member updated = MakeChild();
    updated.profile.taboo_foods = {"牛奶"};
    repo.add_member(updated);
    EXPECT_EQ(stub.detect_and_recognize(nullptr, repo, 0.5, 40)[0].taboo_foods.size(), 1u);
}
