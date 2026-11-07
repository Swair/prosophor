// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/memory_manager.h"
#include "core/agent_session.h"
#include "core/session_key.h"
#include "common/time_wrapper.h"
#include "common/file_utils.h"
#include "config/config.h"

#include <sstream>
#include <regex>
#include <algorithm>
#include <fstream>

namespace prosophor {

// ============================================================================
// 文件写入锁
// ============================================================================
namespace {
std::mutex g_file_mutex;
std::unordered_map<std::string, std::unique_ptr<std::mutex>> g_file_locks;

std::unique_lock<std::mutex> GetFileLock(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_file_mutex);
    auto it = g_file_locks.find(path);
    if (it == g_file_locks.end()) {
        g_file_locks.emplace(path, std::make_unique<std::mutex>());
        it = g_file_locks.find(path);
    }
    return std::unique_lock<std::mutex>(*it->second);
}

std::string BuildExtractionPrompt(const std::vector<MessageSchema>& messages) {
    std::ostringstream p;
    p << "You are a memory consolidation assistant. Analyze the conversation below "
      << "and extract persistent memory entries (facts, rules, preferences, experiences).\n\n"
      << "For each memory point, identify:\n"
      << "1. **TYPE**: fact(事实,如\"用户用Python3.10\") | rule(规则,如\"禁止unsafe代码\") "
      << "| preference(偏好,如\"喜欢简洁回答\") | experience(经验,如\"上次升级OpenSSL挂了\")\n"
      << "2. **SENDER**: the user ID who said the information (each user message below is "
      << "prefixed with its speaker ID; write the exact ID, or 'unknown' if not present)\n"
      << "3. **SCOPE**: user(关于具体用户) | group(关于整个群) | role(助手的经验)\n"
      << "4. **CONTENT**: Brief description\n"
      << "5. **FILES**: Any files mentioned\n\n"
      << "Format your response as:\n"
      << "```\nTYPE: <type>\nSENDER: <id>\nSCOPE: <scope>\nCONTENT: <content>\nFILES: <file1>, <file2>\n```\n\n"
      << "Separate multiple memories with `---`\n\nConversation:\n";
    int start = std::max(0, (int)messages.size() - 50);
    for (size_t i = start; i < messages.size(); ++i) {
        auto& msg = messages[i];
        std::string role = (msg.role == "user") ? "User" : (msg.role == "assistant") ? "Assistant" : msg.role;
        // 群聊/多用户:user 消息带发言者 ID,供 SENDER 归属
        if (msg.role == "user" && !msg.sender_id.empty()) {
            role += "(" + msg.sender_id + ")";
        }
        for (auto& block : msg.content) {
            if (block.type == "text" || block.type == "thinking") {
                auto text = block.text.substr(0, 500);
                p << role << ": " << text << "\n\n";
            } else if (block.type == "tool_use") {
                p << role << " [Using tool: " << block.name << "]\n";
            } else if (block.type == "tool_result") {
                p << "[Tool result]\n\n";
            }
        }
    }
    return p.str();
}

std::string BuildSummaryPrompt(const std::vector<MessageSchema>& messages) {
    std::ostringstream p;
    p << "Please provide a concise summary of this conversation session.\n"
      << "The summary will be stored as long-term memory.\n\nInclude:\n"
      << "1. Key decisions and conclusions\n2. Code changes discussed\n"
      << "3. Files modified\n4. Unresolved issues\n"
      << "5. Important context\n\nKeep it 200-500 words.\n\nConversation:\n";
    int start = std::max(0, (int)messages.size() - 50);
    for (size_t i = start; i < messages.size(); ++i) {
        auto& msg = messages[i];
        std::string role = (msg.role == "user") ? "User" : "Assistant";
        for (auto& block : msg.content)
            if (block.type == "text" || block.type == "thinking")
                p << role << ": " << block.text.substr(0, 500) << "\n\n";
    }
    return p.str();
}

/// 旧四类 → 新四类映射(兼容升级前的提取结果)
std::string NormalizeDecisionType(const std::string& type) {
    if (type == "design_decision" || type == "code_change") return "fact";
    if (type == "lesson_learned" || type == "unresolved_issue") return "experience";
    return type;
}

/// 追加单条记忆到指定文件(带时间戳 + 分类标签,沿用现有 markdown 格式)
void AppendDecisionEntry(const std::filesystem::path& file, const KeyDecision& d,
                         const std::string& line_prefix = "") {
    std::ostringstream e;
    e << "## " << d.timestamp << "\n";
    e << "- **[" << d.type << "]** " << line_prefix << d.content;
    if (!d.related_files.empty()) {
        e << " (Files: ";
        for (size_t i = 0; i < d.related_files.size(); ++i) {
            if (i > 0) e << ", ";
            e << d.related_files[i];
        }
        e << ")";
    }
    e << "\n\n";
    auto lock = GetFileLock(file.string());
    if (!WriteFile(file.string(), e.str(), true))
        LOG_ERROR("Failed write: {}", file.string());
}

std::vector<KeyDecision> ParseDecisions(const std::string& response) {
    std::vector<KeyDecision> decisions;
    std::regex sep(R"(^---\s*$)", std::regex_constants::multiline);
    std::sregex_token_iterator it(response.begin(), response.end(), sep, -1), end;
    std::vector<std::string> segs;
    while (it != end) segs.push_back(*it++);
    std::regex tr(R"(TYPE:\s*(\S+))", std::regex_constants::icase);
    std::regex sr(R"(SENDER:\s*(\S+))", std::regex_constants::icase);
    std::regex scr(R"(SCOPE:\s*(\S+))", std::regex_constants::icase);
    std::regex cr(R"(CONTENT:\s*(.+?)(?=FILES:|SENDER:|SCOPE:|TYPE:|$))", std::regex_constants::icase);
    std::regex fr(R"(FILES:\s*(.+?)(?=TYPE:|SENDER:|SCOPE:|CONTENT:|$))", std::regex_constants::icase);
    for (auto& s : segs) {
        if (s.find_first_not_of(" \t\n\r") == std::string::npos) continue;
        KeyDecision d;
        d.timestamp = SystemClock::GetCurrentTimestamp();
        std::smatch m;
        d.type = (std::regex_search(s, m, tr) && m.size() > 1) ? m[1].str() : "fact";
        d.sender_id = (std::regex_search(s, m, sr) && m.size() > 1) ? m[1].str() : "";
        d.scope = (std::regex_search(s, m, scr) && m.size() > 1) ? m[1].str() : "";
        if (std::regex_search(s, m, cr) && m.size() > 1) {
            d.content = std::regex_replace(m[1].str(), std::regex("^\\s+|\\s+$"), "");
        }
        if (std::regex_search(s, m, fr) && m.size() > 1) {
            std::istringstream fs(m[1].str()); std::string f;
            while (std::getline(fs, f, ',')) {
                f = std::regex_replace(f, std::regex("^\\s+|\\s+$"), "");
                if (!f.empty()) d.related_files.push_back(f);
            }
        }
        if (!d.content.empty()) decisions.push_back(d);
    }
    return decisions;
}

} // anonymous namespace

// ============================================================================
// 从 JSONL 日志读取完整会话消息
// ============================================================================

std::vector<MessageSchema> ReadSessionLogs(const std::string& role_id,
                                           const std::string& owner_filter) {
    std::vector<MessageSchema> messages;
    auto session_dir = ProsophorConfig::BaseDir() / "sessions" / role_id;
    if (!std::filesystem::exists(session_dir)) return messages;

    // 行解析:filter 非空时只保留本归属会话的行
    // (旧数据无 owner 字段 → 仅在 filter 为空时被读出)
    auto read_file = [&messages](const std::filesystem::path& f,
                                 const std::string& filter) {
        std::ifstream in(f);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto j = nlohmann::json::parse(line);
                std::string role = j.value("role", "");
                if (role.empty()) continue;
                if (!filter.empty() && j.value("owner", "") != filter) continue;

                MessageSchema msg;
                msg.role = role;
                msg.sender_id = j.value("sender", "");

                if (j.contains("content")) {
                    msg.AddTextContent(j["content"].get<std::string>());
                }
                if (j.contains("tool")) {
                    std::string tool_name = j["tool"].get<std::string>();
                    auto args = j.value("args", nlohmann::json::object());
                    msg.AddToolUseContent("", tool_name, args);
                }
                if (j.contains("result")) {
                    msg.AddToolResultContent("", j["result"].get<std::string>());
                }

                messages.push_back(std::move(msg));
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to parse session log line: {}", e.what());
            }
        }
    };

    // Path A:新布局 sessions/{role}/{owner}/ — 目录即归属,免行级过滤
    if (!owner_filter.empty()) {
        auto safe_owner = SanitizeIdentity(owner_filter);
        if (safe_owner) {
            auto owner_dir = session_dir / *safe_owner;
            if (std::filesystem::is_directory(owner_dir)) {
                std::vector<std::filesystem::path> files;
                for (const auto& entry : std::filesystem::directory_iterator(owner_dir)) {
                    if (entry.path().extension() == ".jsonl")
                        files.push_back(entry.path());
                }
                std::sort(files.begin(), files.end());  // 文件名排序 = 日期顺序
                for (const auto& f : files) read_file(f, "");
            }
        }
    }

    // Path B:旧布局 sessions/{role}/*.jsonl — 过渡期旧数据与空 owner 归属
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(session_dir)) {
        if (entry.path().extension() == ".jsonl")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& f : files) read_file(f, owner_filter);

    return messages;
}

// ============================================================================
// LLM 回调工厂（从 AgentSession 构造）
// ============================================================================

namespace {
MemoryLlmCallback BuildLlmCallback(AgentSession& session) {
    return [&session](const std::string& prompt) -> std::string {
        ChatRequest req;
        if (auto* r = session.GetRole()) {
            req.model = r->model;
            req.temperature = r->temperature;
            req.max_tokens = 4096;
        }
        if (!session.GetBaseUrl().empty())
            req.base_url = session.GetBaseUrl();
        req.AddUserMessage(
            "你是一个记忆整合助手。从对话中提取持久记忆，分类为：\n"
            "1. fact — 关于用户/群的事实(如\"用户用Python3.10\")\n"
            "2. rule — 用户/群立下的规则(如\"禁止unsafe代码\")\n"
            "3. preference — 用户偏好(如\"喜欢简洁回答\")\n"
            "4. experience — 助手自己的经验教训(如\"上次升级OpenSSL挂了\")\n"
            "忽略：日常问候、临时命令、调试输出。\n"
            "每条记录必须包含 SENDER(发言者 ID)和 SCOPE(user/group/role)归属。\n\n"
            + prompt);
        return session.GetProvider()->Chat(req).content_text;
    };
}
} // anonymous namespace

// ============================================================================
// ConsolidateSegment
// ============================================================================

ConsolidationResult ConsolidateSegment(
    const std::vector<MessageSchema>& messages,
    int start_index, int end_index,
    MemoryLlmCallback llm) {

    ConsolidationResult result;
    if (start_index >= end_index || start_index >= (int)messages.size())
        return result;

    int end = std::min(end_index, (int)messages.size());
    std::vector<MessageSchema> seg;
    for (int i = start_index; i < end; ++i) seg.push_back(messages[i]);
    result.messages_processed = (int)seg.size();
    if (seg.empty()) return result;

    if (llm) {
        try { result.decisions = ParseDecisions(llm(BuildExtractionPrompt(seg)));
              LOG_INFO("Extracted {} decisions", result.decisions.size());
        } catch (std::exception& e) { LOG_ERROR("Extraction failed: {}", e.what()); }
        try { result.summary = llm(BuildSummaryPrompt(seg));
        } catch (std::exception& e) { LOG_ERROR("Summary failed: {}", e.what()); }
    }
    if (result.summary.empty()) {
        std::ostringstream fb;
        fb << "[Segment " << start_index << "-" << end << " " << seg.size() << " msgs]";
        result.summary = fb.str();
    }
    int chars = 0;
    for (auto& msg : seg) for (auto& b : msg.content)
        if (b.type == "text" || b.type == "thinking") chars += b.text.size();
    result.tokens_estimated = chars / 4;
    return result;
}

// ============================================================================
// AppendToRoleMemory
// ============================================================================

void AppendToRoleMemory(const AgentSession& session,
                        const ConsolidationResult& result,
                        const std::string& category) {
    auto* role = session.GetRole();
    if (!role || role->memory_dir.empty()) { LOG_WARN("No memory dir"); return; }

    auto dir = std::filesystem::path(role->memory_dir) / category;
    std::filesystem::create_directories(dir);
    std::ostringstream e;
    e << "## " << SystemClock::GetCurrentTimestamp() << "\n\n";
    if (!result.summary.empty()) e << "### Summary\n\n" << result.summary << "\n\n";
    if (!result.decisions.empty()) {
        e << "### Key Decisions\n\n";
        for (auto& d : result.decisions) {
            e << "- **[" << d.type << "]** " << d.content;
            if (!d.related_files.empty()) {
                e << " (Files: ";
                for (size_t i = 0; i < d.related_files.size(); ++i) {
                    if (i > 0) e << ", ";
                    e << d.related_files[i];
                }
                e << ")";
            }
            e << "\n";
        }
        e << "\n";
    }
    auto file = dir / (SystemClock::GetCurrentDate() + ".md");
    auto lock = GetFileLock(file.string());
    if (!WriteFile(file.string(), e.str(), true))
        LOG_ERROR("Failed write: {}", file.string());
}

// ============================================================================
// AppendDecisionsToMemory — IM 多用户按归属路由写入
// ============================================================================

std::filesystem::path UserMemoryDir(const std::string& raw_user_id) {
    auto safe = SanitizeIdentity(raw_user_id);
    if (!safe) return {};
    return ProsophorConfig::BaseDir() / "memory" / "users" / *safe;
}

std::filesystem::path GroupMemoryDir(const std::string& raw_group_id) {
    auto safe = SanitizeIdentity(raw_group_id);
    if (!safe) return {};
    return ProsophorConfig::BaseDir() / "memory" / "groups" / *safe;
}

void AppendDecisionsToMemory(const AgentSession& session,
                             const ConsolidationResult& result) {
    auto* role = session.GetRole();
    if (!role) { LOG_WARN("AppendDecisionsToMemory: no role"); return; }
    if (result.decisions.empty()) return;

    for (const auto& d : result.decisions) {
        std::string type = NormalizeDecisionType(d.type);
        std::string scope = d.scope;
        std::string owner = d.owner_id;

        // 兜底链(ADR-IM2):scope 空 → 按会话类型;owner 空 → sender → session owner
        if (scope.empty()) scope = session.IsGroupSession() ? "group" : "user";
        if (owner.empty()) owner = d.sender_id;
        if (owner.empty()) owner = session.GetOwnerId();
        if (type != "fact" && type != "rule" && type != "preference") type = "experience";

        // 无法归属的内容降级写角色经历,不外泄到他人记忆(隐私下限)
        if (owner.empty() || scope == "role") {
            KeyDecision exp = d;
            exp.type = "experience";
            auto dir = std::filesystem::path(role->memory_dir) / "experiences";
            std::filesystem::create_directories(dir);
            AppendDecisionEntry(dir / (SystemClock::GetCurrentDate() + ".md"), exp);
            LOG_WARN("Decision unowned, fell back to role experiences: {}", d.content);
            continue;
        }

        auto safe_owner = SanitizeIdentity(owner);
        if (!safe_owner) {
            LOG_WARN("Decision owner failed sanitize, dropped: {}", owner);
            continue;
        }

        if (scope == "group") {
            auto dir = GroupMemoryDir(owner);
            if (dir.empty()) { LOG_WARN("Bad group id, dropped: {}", owner); continue; }
            std::filesystem::create_directories(dir);
            // 群无 preferences 文件 → 归入群 profile 并加 [偏好] 前缀
            std::string filename = (type == "preference") ? "profile.md" : (type == "rule" ? "rules.md" : "profile.md");
            std::string prefix = (type == "preference") ? "[偏好] " : "";
            AppendDecisionEntry(dir / filename, d, prefix);
        } else {  // scope == "user"
            auto dir = UserMemoryDir(owner);
            if (dir.empty()) { LOG_WARN("Bad user id, dropped: {}", owner); continue; }
            std::filesystem::create_directories(dir);
            std::string filename = (type == "rule") ? "rules.md"
                                : (type == "preference") ? "preferences.md"
                                : "profile.md";  // fact → profile.md
            AppendDecisionEntry(dir / filename, d);
        }
    }
}

// ============================================================================
// SaveDailyMemory (free function)
// ============================================================================

void SaveDailyMemory(const std::string& dir, const std::string& content,
                     const std::string& date_str) {
    auto d = date_str.empty() ? SystemClock::GetCurrentDate() : date_str;
    auto mem_dir = std::filesystem::path(dir) / "consolidation";
    std::filesystem::create_directories(mem_dir);
    std::ostringstream e;
    e << "## " << SystemClock::GetCurrentTimestamp() << "\n\n" << content << "\n";
    auto file = mem_dir / (d + ".md");
    auto lock = GetFileLock(file.string());
    if (!WriteFile(file.string(), e.str(), true))
        LOG_ERROR("Failed daily memory write: {}", file.string());
}

// ============================================================================
// LengthMemoryStrategy
// ============================================================================

void LengthMemoryStrategy::TrySegmentConsolidation(AgentSession& session) {
    auto* role = session.GetRole();
    if (!role) return;

    // 从 JSONL 日志 + 当前消息合并为完整会话历史(仅本归属主体的行)
    auto jsonl_msgs = ReadSessionLogs(role->id, session.GetOwnerId());
    const auto& current_msgs = session.GetMessages();

    int total = static_cast<int>(jsonl_msgs.size() + current_msgs.size());
    int consolidated = session.GetConsolidatedCount();

    if (total < consolidated + threshold_) return;

    auto llm = BuildLlmCallback(session);
    if (!llm) return;

    // 合并消息列表
    std::vector<MessageSchema> all_msgs = std::move(jsonl_msgs);
    all_msgs.insert(all_msgs.end(), current_msgs.begin(), current_msgs.end());

    LOG_INFO("Consolidating [{},{}) total={}", consolidated, total, total);
    auto r = ConsolidateSegment(all_msgs, consolidated, total, llm);
    if (r.summary.empty()) {
        std::ostringstream fb;
        fb << "[Segment " << consolidated << "-" << total << "]";
        r.summary = fb.str();
    }
    AppendToRoleMemory(session, r, "segments");
    AppendDecisionsToMemory(session, r);  // decisions 按 user/group/role 归属分写
    session.SetConsolidatedCount(total);
}

// ============================================================================
// ExitMemoryStrategy
// ============================================================================

void ExitMemoryStrategy::TryExitConsolidation(AgentSession& session) {
    auto* role = session.GetRole();
    if (!role) return;

    auto llm = BuildLlmCallback(session);
    if (!llm) return;

    // 从 JSONL 日志 + 当前消息获取完整历史(仅本归属主体的行)
    auto jsonl_msgs = ReadSessionLogs(role->id, session.GetOwnerId());
    const auto& current_msgs = session.GetMessages();

    std::vector<MessageSchema> all_msgs = std::move(jsonl_msgs);
    all_msgs.insert(all_msgs.end(), current_msgs.begin(), current_msgs.end());

    if (all_msgs.empty()) return;
    LOG_INFO("Exit consolidation ({} msgs from log + {} current)",
             all_msgs.size() - current_msgs.size(), current_msgs.size());
    auto r = ConsolidateSegment(all_msgs, 0, (int)all_msgs.size(), llm);
    if (!r.summary.empty()) AppendToRoleMemory(session, r, "exit_summary");
    AppendDecisionsToMemory(session, r);  // decisions 按 user/group/role 归属分写
}

// ============================================================================
// Factory
// ============================================================================

std::shared_ptr<MemoryStrategy> CreateDefaultMemoryStrategy() {
    auto c = std::make_shared<CompositeMemoryStrategy>();
    c->Add(std::make_shared<LengthMemoryStrategy>(30));
    c->Add(std::make_shared<ExitMemoryStrategy>());
    return c;
}

// ============================================================================
// MemoryManager (workspace file management)
// ============================================================================

MemoryManager::MemoryManager(const std::filesystem::path& workspace_path)
    : workspace_path_(workspace_path) {
    base_dir_ = workspace_path.parent_path();
    std::filesystem::create_directories(workspace_path_);
    LOG_INFO("MemoryManager initialized: {}", workspace_path_.string());
}

MemoryManager::~MemoryManager() { StopFileWatcher(); }

void MemoryManager::LoadWorkspaceFiles() {
    LOG_DEBUG("Loading workspace files from {}", workspace_path_.string());
    LoadProsophorFilesRecursively(workspace_path_);
}

std::string MemoryManager::ReadIdentityFile(const std::string& filename) const {
    auto path = workspace_path_ / filename;
    auto content = ReadFile(path.string());
    return content.value_or("");
}

std::string MemoryManager::ReadAgentsFile() const {
    auto path = workspace_path_ / "AGENTS.md";
    auto content = ReadFile(path.string());
    return content.value_or("");
}

std::string MemoryManager::ReadToolsFile() const {
    auto path = workspace_path_ / "TOOLS.md";
    auto content = ReadFile(path.string());
    return content.value_or("");
}

std::vector<std::string> MemoryManager::SearchMemory(const std::string& query) const {
    std::vector<std::string> results;
    if (!std::filesystem::exists(workspace_path_)) return results;
    try {
        std::string lower_query = query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
        for (auto& entry : std::filesystem::recursive_directory_iterator(workspace_path_)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".md" && ext != ".txt") continue;
            auto content = ReadFile(entry.path().string());
            if (content) {
                std::string lower_content = content.value();
                std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);
                if (lower_content.find(lower_query) != std::string::npos)
                    results.push_back(entry.path().string());
            }
        }
    } catch (std::exception& e) {
        LOG_ERROR("SearchMemory error: {}", e.what());
    }
    return results;
}

void MemoryManager::SaveDailyMemory(const std::string& content) {
    auto date_str = SystemClock::GetCurrentDate();
    auto ts = SystemClock::GetCurrentTimestamp();
    auto mem_dir = base_dir_ / "memory";
    std::filesystem::create_directories(mem_dir);
    std::ostringstream e;
    e << "## " << ts << "\n" << content << "\n";
    auto file = mem_dir / (date_str + ".md");
    if (!WriteFile(file.string(), e.str(), true))
        throw std::runtime_error("Failed to write to memory file: " + file.string());
    LOG_DEBUG("Saved memory entry to {}", file.string());
}

void MemoryManager::StartFileWatcher() {
    if (watching_) return;
    watching_ = true;
    watcher_thread_ = std::make_unique<std::thread>([this]() {
        while (watching_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::lock_guard<std::mutex> lock(watcher_mutex_);
            if (!watching_) break;
            try {
                for (auto& entry : std::filesystem::recursive_directory_iterator(workspace_path_)) {
                    if (!entry.is_regular_file()) continue;
                    auto path = entry.path();
                    auto fname = path.filename().string();
                    if (fname == "PROSOPHOR.md" || fname == "AGENTS.md" ||
                        fname == "TOOLS.md" || fname == "SOUL.md" || fname == "USER.md" ||
                        fname == "MEMORY.md") {
                        auto mtime = std::filesystem::last_write_time(path);
                        auto& last = file_mtimes_[path.string()];
                        if (last != std::filesystem::file_time_type{} && last != mtime) {
                            if (change_callback_) change_callback_(path.string());
                        }
                        last = mtime;
                    }
                }
            } catch (...) {}
        }
    });
}

void MemoryManager::StopFileWatcher() {
    if (!watching_) return;
    watching_ = false;
    if (watcher_thread_ && watcher_thread_->joinable()) watcher_thread_->join();
}

void MemoryManager::SetFileChangeCallback(FileChangeCallback cb) {
    change_callback_ = std::move(cb);
}

const std::filesystem::path& MemoryManager::GetWorkspacePath() const {
    return workspace_path_;
}

void MemoryManager::SetAgentWorkspace(const std::string& agent_id) {
    agent_id_ = agent_id;
    workspace_path_ = base_dir_ / "agents" / agent_id / "workspace";
    std::filesystem::create_directories(workspace_path_);
    LOG_DEBUG("SetAgentWorkspace: {}", workspace_path_.string());
}

std::filesystem::path MemoryManager::GetBaseDir() const { return base_dir_; }

std::filesystem::path MemoryManager::GetSessionsDir(const std::string& agent_id) const {
    return base_dir_ / "agents" / agent_id / "sessions";
}

bool MemoryManager::IsMemoryFile(const std::filesystem::path& filepath) const {
    auto name = filepath.filename().string();
    return name == "PROSOPHOR.md" || name == "AGENTS.md" || name == "TOOLS.md" ||
           name == "SOUL.md" || name == "USER.md" || name == "MEMORY.md";
}

std::string MemoryManager::ReadFileContent(const std::filesystem::path& path) const {
    auto content = ReadFile(path.string());
    return content.value_or("");
}

void MemoryManager::WriteFileContent(const std::filesystem::path& path,
                                      const std::string& content) const {
    WriteFile(path.string(), content);
}

void MemoryManager::LoadProsophorFilesRecursively(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) return;
    try {
        for (auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().filename() == "PROSOPHOR.md") {
                LOG_DEBUG("Found PROSOPHOR.md: {}", entry.path().string());
            }
        }
    } catch (std::exception& e) {
        LOG_ERROR("LoadProsophorFiles error: {}", e.what());
    }
}

}  // namespace prosophor
