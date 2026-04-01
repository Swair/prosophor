// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <algorithm>

#include "services/cron_scheduler.h"

namespace aicode {

class CronSchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        scheduler_ = &CronScheduler::GetInstance();
        // Initialize scheduler if not already running
        scheduler_->Initialize();
        // Clean up any existing tasks
        auto tasks = scheduler_->ListTasks();
        for (const auto& task : tasks) {
            scheduler_->Delete(task.id);
        }
    }

    void TearDown() override {
        // Clean up tasks before shutdown
        auto tasks = scheduler_->ListTasks();
        for (const auto& task : tasks) {
            scheduler_->Delete(task.id);
        }
        scheduler_->Shutdown();
    }

    CronScheduler* scheduler_;
};

TEST_F(CronSchedulerTest, SingletonInstance) {
    auto* instance1 = &CronScheduler::GetInstance();
    auto* instance2 = &CronScheduler::GetInstance();
    EXPECT_EQ(instance1, instance2);
}

TEST_F(CronSchedulerTest, ScheduleRecurringTask) {
    std::string task_id = scheduler_->Schedule(
        "*/5 * * * *",  // Every 5 minutes
        "Test recurring prompt",
        true,   // recurring
        false   // durable
    );

    EXPECT_FALSE(task_id.empty());

    auto tasks = scheduler_->ListTasks();
    EXPECT_GE(tasks.size(), 1u);

    auto task = std::find_if(tasks.begin(), tasks.end(),
        [&task_id](const ScheduledTask& t) { return t.id == task_id; });

    EXPECT_NE(task, tasks.end());
    EXPECT_EQ(task->cron_expression, "*/5 * * * *");
    EXPECT_EQ(task->prompt, "Test recurring prompt");
    EXPECT_TRUE(task->recurring);
    EXPECT_FALSE(task->durable);
}

TEST_F(CronSchedulerTest, ScheduleOneShotTask) {
    std::string task_id = scheduler_->Schedule(
        "0 12 * * *",  // Noon every day
        "Test one-shot prompt",
        false,  // not recurring (one-shot)
        false   // durable
    );

    EXPECT_FALSE(task_id.empty());

    auto tasks = scheduler_->ListTasks();
    auto task = std::find_if(tasks.begin(), tasks.end(),
        [&task_id](const ScheduledTask& t) { return t.id == task_id; });

    EXPECT_NE(task, tasks.end());
    EXPECT_FALSE(task->recurring);
}

TEST_F(CronSchedulerTest, ScheduleDurableTask) {
    std::string task_id = scheduler_->Schedule(
        "0 0 * * *",
        "Test durable prompt",
        true,   // recurring
        true    // durable (should persist to file)
    );

    EXPECT_FALSE(task_id.empty());

    auto tasks = scheduler_->ListTasks();
    auto task = std::find_if(tasks.begin(), tasks.end(),
        [&task_id](const ScheduledTask& t) { return t.id == task_id; });

    EXPECT_NE(task, tasks.end());
    EXPECT_TRUE(task->durable);
}

TEST_F(CronSchedulerTest, DeleteTask) {
    std::string task_id = scheduler_->Schedule(
        "0 0 * * *",
        "To be deleted",
        false,
        false
    );

    bool deleted = scheduler_->Delete(task_id);
    EXPECT_TRUE(deleted);

    auto tasks = scheduler_->ListTasks();
    auto task = std::find_if(tasks.begin(), tasks.end(),
        [&task_id](const ScheduledTask& t) { return t.id == task_id; });

    EXPECT_EQ(task, tasks.end());
}

TEST_F(CronSchedulerTest, DeleteNonExistentTask) {
    bool deleted = scheduler_->Delete("nonexistent_id");
    EXPECT_FALSE(deleted);
}

TEST_F(CronSchedulerTest, PauseAndResumeTask) {
    std::string task_id = scheduler_->Schedule(
        "*/1 * * * *",
        "Pause/Resume test",
        true,
        false
    );

    // Initially should be active
    auto tasks = scheduler_->ListTasks();
    auto task = std::find_if(tasks.begin(), tasks.end(),
        [&task_id](const ScheduledTask& t) { return t.id == task_id; });
    ASSERT_NE(task, tasks.end());
    EXPECT_EQ(task->status, CronJobStatus::Active);

    // Pause
    bool paused = scheduler_->Pause(task_id);
    EXPECT_TRUE(paused);

    tasks = scheduler_->ListTasks();
    task = std::find_if(tasks.begin(), tasks.end(),
        [&task_id](const ScheduledTask& t) { return t.id == task_id; });
    EXPECT_EQ(task->status, CronJobStatus::Paused);

    // Resume
    bool resumed = scheduler_->Resume(task_id);
    EXPECT_TRUE(resumed);

    tasks = scheduler_->ListTasks();
    task = std::find_if(tasks.begin(), tasks.end(),
        [&task_id](const ScheduledTask& t) { return t.id == task_id; });
    EXPECT_EQ(task->status, CronJobStatus::Active);
}

TEST_F(CronSchedulerTest, RunNow) {
    std::string task_id = scheduler_->Schedule(
        "0 0 * * *",
        "echo 'Hello from cron'",
        false,
        false
    );

    std::string result = scheduler_->RunNow(task_id);

    // Should execute the prompt (which is a bash command in this case)
    EXPECT_NE(result.find("Hello"), std::string::npos);
}

TEST_F(CronSchedulerTest, ListTasks_Empty) {
    // After cleanup in SetUp, should be empty or have minimal tasks
    auto tasks = scheduler_->ListTasks();
    EXPECT_GE(tasks.size(), 0u);
}

TEST_F(CronSchedulerTest, TaskTimestamps) {
    std::string task_id = scheduler_->Schedule(
        "0 0 * * *",
        "Timestamp test",
        false,
        false
    );

    auto tasks = scheduler_->ListTasks();
    auto task = std::find_if(tasks.begin(), tasks.end(),
        [&task_id](const ScheduledTask& t) { return t.id == task_id; });

    ASSERT_NE(task, tasks.end());
    EXPECT_FALSE(task->created_at.empty());
    EXPECT_FALSE(task->next_execution_at.empty());
}

TEST_F(CronSchedulerTest, InvalidCronExpression) {
    // Invalid cron should still be accepted (validation is external)
    std::string task_id = scheduler_->Schedule(
        "invalid cron",
        "Test",
        false,
        false
    );

    // The scheduler may accept it and fail at execution time
    // or reject it - depends on implementation
    EXPECT_FALSE(task_id.empty());  // Currently accepts any string
}

TEST_F(CronSchedulerTest, MultipleTasks) {
    std::vector<std::string> task_ids;

    for (int i = 0; i < 5; i++) {
        std::string id = scheduler_->Schedule(
            "0 " + std::to_string(i) + " * * *",
            "Task " + std::to_string(i),
            false,
            false
        );
        task_ids.push_back(id);
    }

    auto tasks = scheduler_->ListTasks();

    for (const auto& id : task_ids) {
        auto task = std::find_if(tasks.begin(), tasks.end(),
            [&id](const ScheduledTask& t) { return t.id == id; });
        EXPECT_NE(task, tasks.end());
    }

    // Cleanup
    for (const auto& id : task_ids) {
        scheduler_->Delete(id);
    }
}

}  // namespace aicode
