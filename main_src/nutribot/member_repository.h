// 仓储实现：JSON 成员档案库（<dir>/<name>.json）
//
// 移植自 Python infrastructure/member_repository.py（语义对齐）：
//   - 写入走领域对象全字段直落盘（饮食习惯/口味偏好等不丢失）
//   - 无特征也可建档；merge_face 用于拍照补特征的合并入口
//   - 匹配为余弦相似度（每成员多张特征取最高分；>= threshold 命中）
// 序列化使用系统 nlohmann-json（prosophor 生态既有依赖）。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "member.h"
#include "repositories.h"

namespace prosophor::nr::infra {

class JsonMemberRepository final : public nr::domain::MemberRepository {
public:
    explicit JsonMemberRepository(std::string dir);

    // ---- 领域接口实现 ----
    std::optional<nr::domain::MemberMatch> find_match(
        const std::vector<float>& query_emb, double threshold) override;
    std::vector<nr::domain::Member> list_members() override;
    std::optional<nr::domain::Member> get_member(const std::string& name) override;
    void add_member(const nr::domain::Member& member) override;
    void delete_member(const std::string& name) override;

    // 拍照注册合并：成员已存在 -> 保留画像字段，仅替换人脸特征（返回合并结果）
    nr::domain::Member merge_face(const std::string& name,
                                  std::vector<std::vector<float>> embeddings);

    // Member <-> JSON（序列化/反序列化；测试与未来 Web 端共用）
    static std::string member_to_json(const nr::domain::Member& m);
    static nr::domain::Member member_from_json(const std::string& text);

private:
    std::string path_of(const std::string& name) const;
    std::string dir_;
};

}  // namespace prosophor::nr::infra
