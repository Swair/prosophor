// 人脸识别未接前的"常在场"桩：每帧报告一位档案库成员在场
//
// 存在的唯一理由：MealMonitor 的规则比对要求"已知成员非空"才产提醒事件
// （见 meal_monitor.cc 的 known 判据）—— 人脸链路（aidlite 特征）接入前，
// 用本桩把"看菜 → 比对 → 播报"链路先端到端跑通。人脸识别落地后本文件
// 整体删除，装配处换成真实 FaceRecognitionPort 实现。
//
// 成员档案每帧现查（改档案即时生效，不用重启）；成员不在库时返回空列表
// （无已知成员 = 不产事件），装配处启动时校验并响亮报错 —— 这里不静默兜底
// 造一个假档案，那会让"没建档"这个真实故障看起来像"规则没命中"。
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "member.h"
#include "ports.h"
#include "repositories.h"

namespace prosophor::nr::app {

class StubFaceRecognitionPort final : public FaceRecognitionPort {
public:
    explicit StubFaceRecognitionPort(std::string member_name)
        : member_name_(std::move(member_name)) {}

    std::vector<FaceObservation> detect_and_recognize(
        FrameRef /*frame*/, prosophor::nr::domain::MemberRepository& repository,
        double /*threshold*/, int /*min_size*/) override {
        std::vector<FaceObservation> observations;
        const auto member = repository.get_member(member_name_);
        if (!member.has_value()) return observations;

        FaceObservation observation;
        observation.name = member->name;
        observation.similarity = 1.0;   // 桩：不是识别结果，是"设定就在场"
        observation.health_tags = member->profile.health_tags;
        observation.taboo_foods = member->profile.taboo_foods;
        observations.push_back(std::move(observation));
        return observations;
    }

    void release() override {}

    const std::string& member_name() const { return member_name_; }

private:
    std::string member_name_;
};

}  // namespace prosophor::nr::app
