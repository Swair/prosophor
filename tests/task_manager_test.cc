// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "agents/task_manager.h"

namespace aicode {

class TaskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = &TaskManager::GetInstance();
        // Clear existing tasks
        auto tasks = manager_->GetAllTasks();
        for (const auto& task : tasks) {
            manager_->DeleteTask(task.id);
        }
    }

    TaskManager* manager_;
};

TEST_F(TaskManagerTest, SingletonInstance) {
    auto* instance1 = &TaskManager::GetInstance();
    auto* instance2 = &TaskManager::GetInstance();
    EXPECT_EQ(instance1, instance2);
}

TEST_F(TaskManagerTest, CreateTask) {
    std::string subject = "Test Task";
    std::string description = "This is a test task";
    std::string active_form = "Testing task creation";

    std::string task_id = manager_->CreateTask(subject, description, active_form);

    EXPECT_FALSE(task_id.empty());

    // Verify task was created
    Task* task = manager_->GetTask(task_id);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->subject, subject);
    EXPECT_EQ(task->description, description);
    EXPECT_EQ(task->active_form, active_form);
    EXPECT_EQ(task->status, TaskStatus::Pending);
}

TEST_F(TaskManagerTest, GetTask_NotFound) {
    Task* task = manager_->GetTask("nonexistent_id");
    EXPECT_EQ(task, nullptr);
}

TEST_F(TaskManagerTest, UpdateTaskStatus) {
    std::string task_id = manager_->CreateTask("Test", "Description", "Testing");

    // Update to in_progress
    bool updated = manager_->UpdateTaskStatus(task_id, TaskStatus::InProgress);
    EXPECT_TRUE(updated);

    Task* task = manager_->GetTask(task_id);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->StatusToString(), "in_progress");

    // Update to completed
    updated = manager_->UpdateTaskStatus(task_id, TaskStatus::Completed);
    EXPECT_TRUE(updated);

    task = manager_->GetTask(task_id);
    EXPECT_EQ(task->StatusToString(), "completed");
}

TEST_F(TaskManagerTest, UpdateTaskOwner) {
    std::string task_id = manager_->CreateTask("Test", "Description", "Testing");

    bool updated = manager_->UpdateTaskOwner(task_id, "test_user");
    EXPECT_TRUE(updated);

    Task* task = manager_->GetTask(task_id);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->owner, "test_user");
}

TEST_F(TaskManagerTest, UpdateTaskDescription) {
    std::string task_id = manager_->CreateTask("Test", "Description", "Testing");

    bool updated = manager_->UpdateTaskDescription(task_id, "New Subject");
    EXPECT_TRUE(updated);

    Task* task = manager_->GetTask(task_id);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->subject, "New Subject");
}

TEST_F(TaskManagerTest, SetTaskDependencies) {
    std::string id1 = manager_->CreateTask("Task 1", "Desc", "Testing");
    std::string id2 = manager_->CreateTask("Task 2", "Desc", "Testing");

    // Task 2 is blocked by Task 1
    manager_->SetTaskDependencies(id2, {}, {id1});

    Task* task2 = manager_->GetTask(id2);
    ASSERT_NE(task2, nullptr);
    EXPECT_EQ(task2->blocked_by.size(), 1u);
    EXPECT_EQ(task2->blocked_by[0], id1);
}

TEST_F(TaskManagerTest, DeleteTask) {
    std::string task_id = manager_->CreateTask("Test", "Description", "Testing");

    bool deleted = manager_->DeleteTask(task_id);
    EXPECT_TRUE(deleted);

    Task* task = manager_->GetTask(task_id);
    EXPECT_EQ(task, nullptr);
}

TEST_F(TaskManagerTest, DeleteNonExistentTask) {
    bool deleted = manager_->DeleteTask("nonexistent_id");
    EXPECT_FALSE(deleted);
}

TEST_F(TaskManagerTest, GetAllTasks) {
    // Create multiple tasks
    std::vector<std::string> task_ids;
    for (int i = 0; i < 5; i++) {
        std::string id = manager_->CreateTask(
            "Task " + std::to_string(i),
            "Description " + std::to_string(i),
            "Testing");
        task_ids.push_back(id);
    }

    auto tasks = manager_->GetAllTasks();
    EXPECT_EQ(tasks.size(), 5u);
}

TEST_F(TaskManagerTest, GetTasksByStatus) {
    // Create tasks with different statuses
    std::string id1 = manager_->CreateTask("Task 1", "Desc", "Testing");
    std::string id2 = manager_->CreateTask("Task 2", "Desc", "Testing");
    std::string id3 = manager_->CreateTask("Task 3", "Desc", "Testing");

    manager_->UpdateTaskStatus(id1, TaskStatus::InProgress);
    manager_->UpdateTaskStatus(id2, TaskStatus::Completed);

    auto pending_tasks = manager_->GetTasksByStatus(TaskStatus::Pending);
    EXPECT_EQ(pending_tasks.size(), 1u);  // id3

    auto in_progress_tasks = manager_->GetTasksByStatus(TaskStatus::InProgress);
    EXPECT_GE(in_progress_tasks.size(), 1u);

    auto completed_tasks = manager_->GetTasksByStatus(TaskStatus::Completed);
    EXPECT_GE(completed_tasks.size(), 1u);
}

TEST_F(TaskManagerTest, StatusFromString) {
    EXPECT_EQ(Task::FromString("pending"), TaskStatus::Pending);
    EXPECT_EQ(Task::FromString("in_progress"), TaskStatus::InProgress);
    EXPECT_EQ(Task::FromString("completed"), TaskStatus::Completed);
    EXPECT_EQ(Task::FromString("cancelled"), TaskStatus::Cancelled);
}

TEST_F(TaskManagerTest, StatusToString) {
    Task task;

    task.status = TaskStatus::Pending;
    EXPECT_EQ(task.StatusToString(), "pending");

    task.status = TaskStatus::InProgress;
    EXPECT_EQ(task.StatusToString(), "in_progress");

    task.status = TaskStatus::Completed;
    EXPECT_EQ(task.StatusToString(), "completed");

    task.status = TaskStatus::Cancelled;
    EXPECT_EQ(task.StatusToString(), "cancelled");
}

}  // namespace aicode
