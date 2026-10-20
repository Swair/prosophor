// JsonMemberRepository 实现 —— 对照 Python infrastructure/member_repository.py
#include "storage/member_repository.h"

#include <cmath>
#include <random>

#include <nlohmann/json.hpp>

#include "common/file_utils.h"
#include "common/time_wrapper.h"  // SystemClock

namespace prosophor::nr::infra {

using nlohmann::json;
using nr::domain::Member;

namespace {

// Member -> JSON（对照 Python Member.to_dict 全字段）
json to_json_obj(const Member& m) {
    json j;
    j["member_id"] = m.member_id;
    j["name"] = m.name;
    if (m.profile.age) j["age"] = *m.profile.age;
    else j["age"] = nullptr;
    if (m.profile.gender.empty()) j["gender"] = nullptr;
    else j["gender"] = m.profile.gender;
    if (m.profile.height_cm) j["height_cm"] = *m.profile.height_cm;
    else j["height_cm"] = nullptr;
    if (m.profile.weight_kg) j["weight_kg"] = *m.profile.weight_kg;
    else j["weight_kg"] = nullptr;
    j["health_tags"] = m.profile.health_tags;
    j["taboo_foods"] = m.profile.taboo_foods;
    j["allergies"] = m.profile.allergies;
    j["dri_overrides"] = m.profile.dri_overrides;
    j["eating_habits"] = m.profile.eating_habits;
    j["preferences"] = m.profile.preferences;
    j["face_embeddings"] = m.face_embeddings;
    j["registered_at"] = m.registered_at;
    return j;
}

Member from_json_obj(const json& j) {
    Member m;
    m.member_id = j.value("member_id", "");
    m.name = j.value("name", "");
    auto& p = m.profile;
    if (!j.contains("age") || j["age"].is_null()) p.age = std::nullopt;
    else p.age = j["age"].get<int>();
    if (!j.contains("gender") || j["gender"].is_null())
        p.gender.clear();
    else p.gender = j["gender"].get<std::string>();
    if (j.contains("height_cm") && !j["height_cm"].is_null())
        p.height_cm = j["height_cm"].get<double>();
    if (j.contains("weight_kg") && !j["weight_kg"].is_null())
        p.weight_kg = j["weight_kg"].get<double>();
    if (j.contains("health_tags"))
        p.health_tags = j["health_tags"].get<std::vector<std::string>>();
    if (j.contains("taboo_foods"))
        p.taboo_foods = j["taboo_foods"].get<std::vector<std::string>>();
    if (j.contains("allergies"))
        p.allergies = j["allergies"].get<std::vector<std::string>>();
    if (j.contains("dri_overrides"))
        p.dri_overrides =
            j["dri_overrides"].get<std::unordered_map<std::string, double>>();
    if (j.contains("eating_habits"))
        p.eating_habits = j["eating_habits"].get<std::vector<std::string>>();
    if (j.contains("preferences"))
        p.preferences = j["preferences"].get<std::vector<std::string>>();
    if (j.contains("face_embeddings"))
        m.face_embeddings =
            j["face_embeddings"].get<std::vector<std::vector<float>>>();
    m.registered_at = j.value("registered_at", "");
    return m;
}

std::string now_iso() {
    return prosophor::SystemClock::GetCurrentTimestamp();
}

std::string gen_id() {   // 8 位随机 id（Python uuid4()[:8] 近似）
    static const char* hex_digits = "0123456789abcdef";
    std::mt19937 rng{std::random_device{}()};
    std::string s;
    for (int i = 0; i < 8; ++i) s.push_back(hex_digits[rng() % 16]);
    return s;
}

double cosine(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || a.size() != b.size()) return 0.0;
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    if (na <= 0.0 || nb <= 0.0) return 0.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

}  // namespace

// ---- 序列化（public static，供测试/Web 复用）----

std::string JsonMemberRepository::member_to_json(const Member& m) {
    return to_json_obj(m).dump(2);
}

Member JsonMemberRepository::member_from_json(const std::string& text) {
    return from_json_obj(json::parse(text));
}

// ---- 仓储 ----

JsonMemberRepository::JsonMemberRepository(std::string dir)
    : dir_(std::move(dir)) {}

std::string JsonMemberRepository::path_of(const std::string& name) const {
    return prosophor::JoinPath(dir_, name + ".json");
}

std::optional<Member> JsonMemberRepository::get_member(const std::string& name) {
    const auto j = prosophor::ReadJson(path_of(name));
    if (!j.has_value()) return std::nullopt;   // 不存在/坏档案
    return from_json_obj(*j);
}

std::vector<Member> JsonMemberRepository::list_members() {
    std::vector<Member> out;
    for (const auto& name : prosophor::ListDir(dir_, ".json")) {
        const auto j = prosophor::ReadJson(prosophor::JoinPath(dir_, name));
        if (!j.has_value()) continue;   // 单个坏档案不影响整体列举
        out.push_back(from_json_obj(*j));
    }
    return out;
}

std::optional<nr::domain::MemberMatch> JsonMemberRepository::find_match(
    const std::vector<float>& query_emb, double threshold) {
    Member best{};
    double best_sim = 0.0;
    bool hit = false;
    for (const auto& m : list_members()) {
        for (const auto& emb : m.face_embeddings) {
            const double sim = cosine(query_emb, emb);
            if (sim >= threshold && sim > best_sim) {
                best = m;
                best_sim = sim;
                hit = true;
            }
        }
    }
    if (!hit) return std::nullopt;
    nr::domain::MemberMatch match;
    match.member = std::move(best);
    match.similarity = best_sim;
    return match;
}

void JsonMemberRepository::add_member(const Member& member) {
    Member m = member;
    if (m.member_id.empty()) m.member_id = gen_id();
    if (m.registered_at.empty()) m.registered_at = now_iso();
    prosophor::WriteJson(path_of(m.name), to_json_obj(m));
}

void JsonMemberRepository::delete_member(const std::string& name) {
    prosophor::RemoveFile(path_of(name));
}

Member JsonMemberRepository::merge_face(
    const std::string& name,
    std::vector<std::vector<float>> embeddings) {
    Member m = get_member(name).value_or(Member{});
    if (m.name.empty()) m.name = name;   // 新成员以 name 建档
    if (m.member_id.empty()) m.member_id = gen_id();
    if (m.registered_at.empty()) m.registered_at = now_iso();
    m.face_embeddings = std::move(embeddings);   // Python 语义：整组替换
    prosophor::WriteJson(path_of(m.name), to_json_obj(m));
    return m;
}

}  // namespace prosophor::nr::infra
