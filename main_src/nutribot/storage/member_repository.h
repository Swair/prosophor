// 仓储实现：JSON 成员档案库（<dir>/<name>.json）
//
// 移植自 Python infrastructure/member_repository.py（语义对齐）：
//   - 写入走领域对象全字段直落盘（饮食习惯/口味偏好等不丢失）
//   - 不做人脸识别：身份相关字段已移除，只保留健康档案读写
// 序列化使用系统 nlohmann-json（prosophor 生态既有依赖）。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain/member.h"

namespace prosophor::nr::infra {

class JsonMemberRepository final {
public:
    explicit JsonMemberRepository(std::string dir);

    // ---- 成员档案读写 ----
    std::vector<nr::domain::Member> list_members();
    std::optional<nr::domain::Member> get_member(const std::string& name);
    void add_member(const nr::domain::Member& member);
    void delete_member(const std::string& name);

    // Member <-> JSON（序列化/反序列化；测试与未来 Web 端共用）
    static std::string member_to_json(const nr::domain::Member& m);
    static nr::domain::Member member_from_json(const std::string& text);

private:
    std::string path_of(const std::string& name) const;
    std::string dir_;
};

}  // namespace prosophor::nr::infra
