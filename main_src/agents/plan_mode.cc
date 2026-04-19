// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "agents/plan_mode.h"

#include <sstream>
#include <chrono>
#include <iomanip>

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"

namespace aicode {

PlanModeManager& PlanModeManager::GetInstance() {
    static PlanModeManager instance;
    return instance;
}

void PlanModeManager::EnterPlanMode() {
    in_plan_mode_ = true;
    LOG_INFO("Entered plan mode");
}

void PlanModeManager::ExitPlanMode() {
    in_plan_mode_ = false;
    ClearPlan();
    LOG_INFO("Exited plan mode");
}

void PlanModeManager::CreatePlan(const std::string& title, const std::string& description) {
    current_plan_ = Plan();
    current_plan_.title = title;
    current_plan_.description = description;
    current_plan_.created_at = SystemClock::GetCurrentTimestamp();
    current_plan_.updated_at = current_plan_.created_at;
    LOG_INFO("Created plan: {}", title);
}

void PlanModeManager::AddStep(const std::string& description,
                               const std::string& tool_name,
                               const nlohmann::json& tool_args) {
    PlanStep step;
    step.id = static_cast<int>(current_plan_.steps.size()) + 1;
    step.description = description;
    step.tool_name = tool_name;
    step.tool_args = tool_args;
    step.status = PlanStepStatus::Pending;

    current_plan_.steps.push_back(step);
    current_plan_.updated_at = SystemClock::GetCurrentTimestamp();
    LOG_DEBUG("Added plan step {}: {}", step.id, description);
}

void PlanModeManager::ApprovePlan() {
    current_plan_.approved = true;
    current_plan_.updated_at = SystemClock::GetCurrentTimestamp();
    LOG_INFO("Plan approved: {}", current_plan_.title);
}

void PlanModeManager::RejectPlan() {
    current_plan_.approved = false;
    LOG_INFO("Plan rejected: {}", current_plan_.title);
}

void PlanModeManager::CompleteStep(int step_id, const std::string& result) {
    PlanStep* step = current_plan_.GetStep(step_id);
    if (step) {
        step->status = PlanStepStatus::Completed;
        step->result = result;
        current_plan_.updated_at = SystemClock::GetCurrentTimestamp();
        LOG_INFO("Completed plan step {}: {}", step_id, step->description);

        if (current_plan_.IsComplete()) {
            current_plan_.completed = true;
            LOG_INFO("Plan completed: {}", current_plan_.title);
        }
    }
}

void PlanModeManager::FailStep(int step_id, const std::string& error) {
    PlanStep* step = current_plan_.GetStep(step_id);
    if (step) {
        step->status = PlanStepStatus::Failed;
        step->error = error;
        current_plan_.updated_at = SystemClock::GetCurrentTimestamp();
        LOG_ERROR("Failed plan step {}: {} - {}", step_id, step->description, error);
    }
}

void PlanModeManager::SkipStep(int step_id) {
    PlanStep* step = current_plan_.GetStep(step_id);
    if (step) {
        step->status = PlanStepStatus::Skipped;
        current_plan_.updated_at = SystemClock::GetCurrentTimestamp();
        LOG_INFO("Skipped plan step {}: {}", step_id, step->description);
    }
}

void PlanModeManager::ClearPlan() {
    current_plan_ = Plan();
    LOG_DEBUG("Plan cleared");
}

std::string PlanModeManager::GetPlanAsMarkdown() const {
    std::ostringstream oss;

    oss << "# Plan: " << current_plan_.title << "\n\n";
    oss << current_plan_.description << "\n\n";

    if (!current_plan_.approved) {
        oss << "**Status**: Pending approval\n\n";
    } else if (current_plan_.completed) {
        oss << "**Status**: Completed (" << current_plan_.GetProgressPercent() << "%)\n\n";
    } else {
        oss << "**Status**: In progress (" << current_plan_.GetProgressPercent() << "%)\n\n";
    }

    oss << "## Steps\n\n";

    for (const auto& step : current_plan_.steps) {
        std::string icon;
        switch (step.status) {
            case PlanStepStatus::Pending: icon = "- [ ]"; break;
            case PlanStepStatus::InProgress: icon = "- [~]"; break;
            case PlanStepStatus::Completed: icon = "- [x]"; break;
            case PlanStepStatus::Skipped: icon = "- [-]"; break;
            case PlanStepStatus::Failed: icon = "- [!]"; break;
        }

        oss << icon << " " << step.description;

        if (!step.tool_name.empty()) {
            oss << " *(" << step.tool_name << ")*";
        }

        if (!step.result.empty()) {
            oss << "\n    > Result: " << step.result;
        }

        if (!step.error.empty()) {
            oss << "\n    > Error: " << step.error;
        }

        oss << "\n";
    }

    return oss.str();
}

}  // namespace aicode
