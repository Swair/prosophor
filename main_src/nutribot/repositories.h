// 仓储接口（Port）：成员档案库的抽象（移植自 Python domain/repositories.py）
//
// 领域层只依赖此接口，不关心存储介质。
// 实现（JSON 落盘 / 人脸特征匹配）在 infrastructure 层，接入官方序列化与
// 官方 face-embedding 模型后补齐 —— 见 docs/model-sourcing.md。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "member.h"

namespace prosophor::nr::domain {

// 特征匹配结果：命中的成员（值拷贝，避免仓储内部对象失效）+ 相似度（0-1）
struct MemberMatch {
    Member member;
    double similarity = 0.0;
};

class MemberRepository {
public:
    virtual ~MemberRepository() = default;

    // 特征匹配：查询特征与全部成员比对（官方 face-embedding 模型就绪后实现）
    virtual std::optional<MemberMatch> find_match(
        const std::vector<float>& query_emb, double threshold) = 0;

    // 所有成员档案（Member 列表）
    virtual std::vector<Member> list_members() = 0;

    virtual std::optional<Member> get_member(const std::string& name) = 0;
    virtual void add_member(const Member& member) = 0;
    virtual void delete_member(const std::string& name) = 0;
};

}  // namespace prosophor::nr::domain
