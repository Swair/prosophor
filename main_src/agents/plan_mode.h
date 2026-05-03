// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace prosophor {

/// Plan step status
enum class PlanStepStatus {
    Pending,
    InProgress,
    Completed,
    Skipped,
    Failed
};

/// A single step in the plan
struct PlanStep {
    int id = 0;
    std::string description;
    std::string tool_name;
    nlohmann::json tool_args;
    PlanStepStatus status = PlanStepStatus::Pending;
    std::string result;
    std::string error;

    std::string StatusToString() const {
        switch (status) {
            case PlanStepStatus::Pending: return "pending";
            case PlanStepStatus::InProgress: return "in_progress";
            case PlanStepStatus::Completed: return "completed";
            case PlanStepStatus::Skipped: return "skipped";
            case PlanStepStatus::Failed: return "failed";
        }
        return "unknown";
    }
};

/// Plan definition
struct Plan {
    std::string title;
    std::string description;
    std::vector<PlanStep> steps;
    bool approved = false;
    bool completed = false;
    std::string created_at;
    std::string updated_at;

    /// Get next pending step
    PlanStep* GetNextPendingStep() {
        for (auto& step : steps) {
            if (step.status == PlanStepStatus::Pending) {
                return &step;
            }
        }
        return nullptr;
    }

    /// Get step by ID
    PlanStep* GetStep(int id) {
        for (auto& step : steps) {
            if (step.id == id) {
                return &step;
            }
        }
        return nullptr;
    }

    /// Check if all steps are completed
    bool IsComplete() const {
        for (const auto& step : steps) {
            if (step.status != PlanStepStatus::Completed &&
                step.status != PlanStepStatus::Skipped) {
                return false;
            }
        }
        return true;
    }

    /// Get progress percentage
    int GetProgressPercent() const {
        if (steps.empty()) return 0;
        int done = 0;
        for (const auto& step : steps) {
            if (step.status == PlanStepStatus::Completed ||
                step.status == PlanStepStatus::Skipped) {
                done++;
            }
        }
        return (done * 100) / static_cast<int>(steps.size());
    }
};

/// Plan mode manager
class PlanModeManager {
public:
    static PlanModeManager& GetInstance();

    /// Enter plan mode
    void EnterPlanMode();

    /// Exit plan mode
    void ExitPlanMode();

    /// Check if in plan mode
    bool IsInPlanMode() const { return in_plan_mode_; }

    /// Create a new plan
    void CreatePlan(const std::string& title, const std::string& description);

    /// Add a step to the current plan
    void AddStep(const std::string& description,
                 const std::string& tool_name = "",
                 const nlohmann::json& tool_args = nlohmann::json::object());

    /// Get current plan
    const Plan* GetCurrentPlan() const { return &current_plan_; }

    /// Get mutable current plan
    Plan* GetCurrentPlanMutable() { return &current_plan_; }

    /// Approve current plan
    void ApprovePlan();

    /// Reject current plan
    void RejectPlan();

    /// Mark step as completed
    void CompleteStep(int step_id, const std::string& result = "");

    /// Mark step as failed
    void FailStep(int step_id, const std::string& error = "");

    /// Skip a step
    void SkipStep(int step_id);

    /// Clear current plan
    void ClearPlan();

    /// Get plan as markdown
    std::string GetPlanAsMarkdown() const;

private:
    PlanModeManager() = default;
    ~PlanModeManager() = default;

    bool in_plan_mode_ = false;
    Plan current_plan_;
};

}  // namespace prosophor
