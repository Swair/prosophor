// 在场成员路由：语气自适应（书 2.3/3.3）与访客静默（书 1.2/3.3）的纯逻辑
//
// 两件事同源 —— "此刻桌边坐着谁"：
//   语气自适应 —— 把在场成员档案（年龄/健康标签）压成一段提示词块，
//                 随对话消息注入 LLM，使回话按人调整语气（儿童活泼、
//                 老人温和、慢病成员克制）。块随消息走、不进 system
//                 prompt：system 保持稳定，KV 前缀缓存不受影响。
//   访客静默 —— 本帧感知未识别出任何注册成员（客人或只剩访客）时，
//               主动播报（报菜名/提醒/小结）整体转静默；对话被点名仍回应。
//
// 零 IO、零线程：判定与拼装都是纯函数，单测直接喂档案——
// 真人脸接入后语义不变，只换"在场成员"的数据来源。
// 姓名是唯一关联键：感知给姓名（FaceObservation.name），档案由仓储查得。
#pragma once

#include <string>
#include <vector>

#include "domain/events.h"
#include "domain/member.h"

namespace prosophor::nr::app {

/// 按档案压出语气档（儿童 → "活泼鼓励"；糖尿病/高血压/高血脂标签 →
/// "克制提醒，语气温和不吓人"；60 岁及以上 → "温暖放慢"；其余 →
/// "简洁数据"）。慢病标签优先于年龄档。
std::string MemberToneHint(const prosophor::nr::domain::Member& member);

/// 儿童优先的直呼名：表里若有儿童成员返回其名；否则空串（成人不直呼，
/// 保持"简洁数据"的档位——模板话术里成人直呼显得啰嗦）。
/// 供主动播报模板挑选称呼（书 2.3 "对孩子用有趣的语气引导"）。
std::string DirectAddress(const std::vector<prosophor::nr::domain::Member>& members);

/// 拼装注入对话消息的成员上下文块；末尾带换行，直接拼接在用户文本之前。
/// 空列表 → "未识别到已建档的家庭成员（可能是客人）" —— 让 LLM 对访客
/// 场景知情（访客静默下仍可能有对话，见角色 prompt 的访客约定）。
std::string ComposeDinerPromptBlock(
    const std::vector<prosophor::nr::domain::Member>& members);

/// 访客静默判据：本帧感知无任何已识别成员（名字非空）→ true。
/// 调用方据此关掉主动播报；对话路径不受影响（被点名要能回应）。
bool VisitorSilence(
    const std::vector<prosophor::nr::domain::MemberBrief>& present_members);

}  // namespace prosophor::nr::app
