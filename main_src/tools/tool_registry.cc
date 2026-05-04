// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "tools/tool_registry.h"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "common/curl_client.h"
#include "platform/platform.h"
#include "managers/token_tracker.h"
#include "managers/permission_manager.h"
#include "mcp/mcp_client.h"
#include "tools/command_tools/background_run_tool.h"

namespace prosophor {

/// Format command result with exit code (DRY helper)
/// @param result Command output
/// @param status Exit code
/// @param error_prefix Optional error prefix when status != 0
/// @return Formatted result string
static std::string FormatCommandResult(const std::string& result, int status,
                                        const std::string& error_prefix = "") {
    if (status != 0 && !error_prefix.empty()) {
        return error_prefix + "[Exit code: " + std::to_string(status) + "]";
    }
    return result + "[Exit code: " + std::to_string(status) + "]";
}

/// Execute a shell command and capture output (cross-platform, thread-safe)
static std::pair<std::string, int> ExecuteCommand(const std::string& command,
                                                   int timeout_seconds = 0,
                                                   const std::string& workdir = "") {
    auto r = platform::RunCommandWithOutput(command, timeout_seconds, workdir);
    return {r.output, r.exit_code};
}

ToolRegistry& ToolRegistry::GetInstance() {
    static ToolRegistry instance;
    return instance;
}

ToolRegistry::ToolRegistry()
    : workspace_path_("~/.prosophor/workspace") {
    LOG_DEBUG("ToolRegistry initialized");

    // Initialize permission manager with default config
    auto& perm_manager = PermissionManager::GetInstance();
    perm_manager.Initialize();

    // Register built-in tools - ensures GetInstance() singleton has all tools
    RegisterBuiltinTools();
}

void ToolRegistry::RegisterBuiltinTools() {
    // read_file
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        RegisterTool("read_file", "Read content of a file", params,
                     [this](const nlohmann::json& p) { return ReadFileTool(p); });
    }

    // write_file
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        params["content"] = nlohmann::json::object();
        params["content"]["type"] = "string";
        RegisterTool("write_file", "Write content to a file", params,
                     [this](const nlohmann::json& p) { return WriteFileTool(p); });
    }

    // edit_file
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        params["old_text"] = nlohmann::json::object();
        params["old_text"]["type"] = "string";
        params["new_text"] = nlohmann::json::object();
        params["new_text"]["type"] = "string";
        RegisterTool("edit_file", "Edit a file with search/replace", params,
                     [this](const nlohmann::json& p) { return EditFileTool(p); });
    }

    // bash
    {
        nlohmann::json params = nlohmann::json::object();
        params["command"] = nlohmann::json::object();
        params["command"]["type"] = "string";
        RegisterTool("bash", "Execute a bash command", params,
                     [this](const nlohmann::json& p) { return BashTool(p); });
    }

    // background_run - manage long-running background shell sessions
    {
        nlohmann::json params = nlohmann::json::object();
        params["action"] = nlohmann::json::object();
        params["action"]["type"] = "string";
        params["action"]["enum"] = {"run", "get", "list", "cancel", "drain"};
        params["action"]["description"] = "Action: run (start command), get (status), list (all), cancel (stop), drain (notifications)";
        params["command"] = nlohmann::json::object();
        params["command"]["type"] = "string";
        params["command"]["description"] = "Shell command to run (required for run action)";
        params["cwd"] = nlohmann::json::object();
        params["cwd"]["type"] = "string";
        params["cwd"]["description"] = "Working directory (required for run action)";
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Task ID (required for get/cancel actions)";
        RegisterTool("background_run", "Manage long-running background shell sessions", params,
                     [this](const nlohmann::json& p) { return BackgroundRunTool(p); });
    }

    // web_search
    {
        nlohmann::json params = nlohmann::json::object();
        params["query"] = nlohmann::json::object();
        params["query"]["type"] = "string";
        params["query"]["description"] = "Search query";
        params["count"] = nlohmann::json::object();
        params["count"]["type"] = "integer";
        params["count"]["description"] = "Number of results (1-10, default 5)";
        RegisterTool("web_search", "Search the web using Brave, Tavily, or DuckDuckGo", params,
                     [this](const nlohmann::json& p) { return WebSearchTool(p); });
    }

    // web_fetch
    {
        nlohmann::json params = nlohmann::json::object();
        params["url"] = nlohmann::json::object();
        params["url"]["type"] = "string";
        RegisterTool("web_fetch", "Fetch content from a URL", params,
                     [this](const nlohmann::json& p) { return WebFetchTool(p); });
    }

    // memory_search - Search workspace memory files (MEMORY.md, docs)
    {
        nlohmann::json params = nlohmann::json::object();
        params["query"] = nlohmann::json::object();
        params["query"]["type"] = "string";
        params["query"]["description"] = "Search query (keyword match)";
        params["max_results"] = nlohmann::json::object();
        params["max_results"]["type"] = "integer";
        params["max_results"]["description"] = "Max results to return (default 10)";
        RegisterTool("memory_search", "Search agent memory files (MEMORY.md and workspace docs) using keyword matching", params,
                     [this](const nlohmann::json& p) { return MemorySearchTool(p); });
    }

    // memory_get - Read a specific file from workspace
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        params["path"]["description"] = "Relative path within workspace (e.g. MEMORY.md or docs/notes.md)";
        RegisterTool("memory_get", "Read a specific file from the agent workspace (MEMORY.md, notes, etc.)", params,
                     [this](const nlohmann::json& p) { return MemoryGetTool(p); });
    }

    // apply_patch - Apply multi-file patches in *** Begin Patch format
    {
        nlohmann::json params = nlohmann::json::object();
        params["patch"] = nlohmann::json::object();
        params["patch"]["type"] = "string";
        params["patch"]["description"] = "Patch text in *** Begin Patch ... *** End Patch format";
        RegisterTool("apply_patch", "Apply a multi-file patch in *** Begin Patch / *** End Patch format. Supports: *** Add File, *** Update File (unified diff hunks), *** Delete File", params,
                     [this](const nlohmann::json& p) { return ApplyPatchTool(p); });
    }

    // token_count - Count tokens in text
    {
        nlohmann::json params = nlohmann::json::object();
        params["text"] = nlohmann::json::object();
        params["text"]["type"] = "string";
        params["model"] = nlohmann::json::object();
        params["model"]["type"] = "string";
        params["model"]["description"] = "Model name for token counting";
        RegisterTool("token_count", "Count tokens in text", params,
                     [this](const nlohmann::json& p) { return TokenCountTool(p); });
    }

    // token_usage - Get token usage statistics
    {
        nlohmann::json params = nlohmann::json::object();
        params["model"] = nlohmann::json::object();
        params["model"]["type"] = "string";
        params["model"]["description"] = "Model name (optional, omit for total)";
        RegisterTool("token_usage", "Get token usage statistics", params,
                     [this](const nlohmann::json& p) { return TokenUsageTool(p); });
    }

    // Register MCP tools dynamically
    // RegisterMcpTools();

    LOG_DEBUG("Registered built-in tools", tool_schemas_.size());
}

void ToolRegistry::RegisterTool(
    const std::string& name, const std::string& description,
    nlohmann::json parameters,
    std::function<std::string(const nlohmann::json&)> executor) {
    tools_[name] = executor;

    ToolsSchema schema;
    schema.name = name;
    schema.description = description;

    // Wrap parameters in OpenAI-compatible function schema format
    nlohmann::json wrapped_schema = nlohmann::json::object();
    wrapped_schema["type"] = "object";
    wrapped_schema["properties"] = std::move(parameters);

    // Extract required fields (all defined properties are required by default)
    nlohmann::json required = nlohmann::json::array();
    for (const auto& [key, value] : wrapped_schema["properties"].items()) {
        required.push_back(key);
    }
    wrapped_schema["required"] = required;

    schema.input_schema = std::move(wrapped_schema);
    tool_schemas_.push_back(schema);

    LOG_DEBUG("Registered tool: {}", name);
}

std::vector<ToolsSchema> ToolRegistry::GetToolSchemas() const {
    return tool_schemas_;
}

bool ToolRegistry::HasTool(const std::string& tool_name) const {
    return tools_.find(tool_name) != tools_.end();
}

void ToolRegistry::SetWorkspace(const std::string& path) {
    workspace_path_ = path;
    LOG_DEBUG("Set workspace: {}", workspace_path_);
}

void ToolRegistry::SetPermissionConfirmCallback(PermissionManager::ConfirmCallback cb) {
    auto& perm_manager = PermissionManager::GetInstance();
    perm_manager.SetConfirmCallback(std::move(cb));
    LOG_DEBUG("Permission confirmation callback set");
}

std::string ToolRegistry::ExecuteTool(const std::string& tool_name,
                                      const nlohmann::json& parameters) {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) {
        throw std::runtime_error("Executing tool: not found: " + tool_name);
    }

    // Check permissions before executing tool
    auto& perm_manager = PermissionManager::GetInstance();
    auto perm_result = perm_manager.CheckPermission(tool_name, parameters);

    if (perm_result.level == PermissionLevel::Deny) {
        LOG_WARN("Executing tool: {} denied: {}", tool_name, perm_result.reason);
        throw std::runtime_error("Tool execution denied: " + perm_result.reason);
    }

    LOG_DEBUG("Executing tool: {} - Permission check passed", tool_name);

    if (perm_result.level == PermissionLevel::Ask) {
        // Request user confirmation
        if (!perm_manager.RequestUserConfirmation(tool_name, parameters, perm_result.reason)) {
            LOG_WARN("Executing tool: {} confirmation denied by user", tool_name);
            throw std::runtime_error("Executing tool: execution denied by user");
        }
    }
    LOG_DEBUG("Executing tool: {} - Permission granted", tool_name);
    LOG_DEBUG("Executing tool: {}", tool_name);

    std::string ret = it->second(parameters);

    LOG_DEBUG("Executing tool: {} execution result: {}", tool_name, ret);
    return ret;
}

std::string ToolRegistry::ReadFileTool(const nlohmann::json& params) {
    std::string path = params.value("path", "");
    if (path.empty()) {
        throw std::runtime_error("Path is required");
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

std::string ToolRegistry::WriteFileTool(const nlohmann::json& params) {
    std::string path = params.value("path", "");
    std::string content = params.value("content", "");

    if (path.empty()) {
        throw std::runtime_error("Path is required");
    }

    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write to file: " + path);
    }

    file << content;
    return "File written successfully: " + path;
}

std::string ToolRegistry::EditFileTool(const nlohmann::json& params) {
    std::string path = params.value("path", "");
    std::string old_text = params.value("old_text", "");
    std::string new_text = params.value("new_text", "");

    if (path.empty() || old_text.empty()) {
        throw std::runtime_error("Path and old_text are required");
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    size_t pos = content.find(old_text);
    if (pos == std::string::npos) {
        throw std::runtime_error("Old text not found in file");
    }

    content.replace(pos, old_text.length(), new_text);

    std::ofstream out_file(path);
    if (!out_file.is_open()) {
        throw std::runtime_error("Cannot write to file: " + path);
    }

    out_file << content;
    return "File edited successfully: " + path;
}

std::string ToolRegistry::BashTool(const nlohmann::json& params) {
    std::string command = params.value("command", "");
    if (command.empty()) {
        throw std::runtime_error("BashTool: Command is required");
    }

    // Extract optional parameters
    int timeout = params.value("timeout", 30);  // Default 30s timeout
    std::string workdir = params.value("workdir", "");

    auto [result, status] = ExecuteCommand(command, timeout, workdir);

    // Handle timeout (-2) or fatal error (-1)
    if (status == -2) {
        throw std::runtime_error("BashTool: Command timeout after " + std::to_string(timeout) + "s: " + command);
    }
    if (status == -1) {
        throw std::runtime_error("BashTool: Failed to execute command: " + command);
    }

    // Always return output + exit code, even if non-zero
    // Let the LLM decide how to interpret the result
    return FormatCommandResult(result, status);
}

std::string ToolRegistry::WebFetchTool(const nlohmann::json& params) {
    std::string url = params.value("url", "");
    if (url.empty()) {
        throw std::runtime_error("URL is required");
    }

    // Validate URL scheme
    if (url.find("http://") != 0 && url.find("https://") != 0) {
        return "Error: Invalid URL scheme. URL must start with http:// or https://";
    }

    // Use curl to fetch the URL
    HttpRequest req;
    req.url = url;
    req.timeout_seconds = 30;
    req.user_agent = "Prosophor/1.0 (WebFetch Tool)";

    HttpResponse resp = HttpClient::Instance().Post(req);

    if (resp.failed()) {
        return "Error fetching URL: " + resp.error_msg + " (HTTP " + std::to_string(resp.status_code) + ")";
    }

    // Simple HTML to text conversion - strip tags
    std::string text;
    bool in_tag = false;
    for (char c : resp.body) {
        if (c == '<') {
            in_tag = true;
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag) {
            text += c;
        }
    }

    // Clean up whitespace
    std::string result;
    bool last_was_space = true;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
        }
    }

    // Limit output size
    const size_t MAX_OUTPUT = 50000;
    if (result.size() > MAX_OUTPUT) {
        result = result.substr(0, MAX_OUTPUT) + "\n\n[Content truncated...]";
    }

    return result;
}

// URL decode helper
static std::string url_decode(const std::string& input) {
    std::ostringstream result;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            unsigned int hi = 0;
            if (std::sscanf(input.substr(i + 1, 2).c_str(), "%x", &hi) == 1) {
                result << static_cast<char>(hi);
                i += 2;
            } else {
                result << input[i];
            }
        } else if (input[i] == '+') {
            result << ' ';
        } else {
            result << input[i];
        }
    }
    return result.str();
}

// Simple HTML to text conversion (no regex to avoid <regex> dependency)
static std::string html_to_text(const std::string& html) {
    std::string text = html;
    // Simple string replacements for HTML entities
    auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replace_all(text, "&nbsp;", " ");
    replace_all(text, "&amp;", "&");
    replace_all(text, "&lt;", "<");
    replace_all(text, "&gt;", ">");
    replace_all(text, "&quot;", "\"");
    replace_all(text, "&#39;", "'");

    // Strip HTML tags
    std::string result;
    bool in_tag = false;
    for (char c : text) {
        if (c == '<') {
            in_tag = true;
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag) {
            result += c;
        }
    }

    // Clean whitespace
    std::string cleaned;
    bool last_space = true;
    for (char c : result) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!last_space) {
                cleaned += ' ';
                last_space = true;
            }
        } else {
            cleaned += c;
            last_space = false;
        }
    }

    // Trim
    if (!cleaned.empty() && cleaned.front() == ' ') cleaned = cleaned.substr(1);
    if (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();

    return cleaned;
}

std::string ToolRegistry::WebSearchTool(const nlohmann::json& params) {
    std::string query = params.value("query", "");
    int count = params.value("count", 5);
    if (query.empty()) {
        throw std::runtime_error("Query is required");
    }
    if (count < 1) count = 5;
    if (count > 10) count = 10;

    // Get API keys from environment
    const char* brave_key = std::getenv("BRAVE_API_KEY");
    const char* tavily_key = std::getenv("TAVILY_API_KEY");

    std::string last_error;
    std::ostringstream result;

    // --- Brave Search (API key required) ---
    if (brave_key && *brave_key) {
        try {
            char* escaped_raw = curl_easy_escape(nullptr, query.c_str(), (int)query.length());
            std::string escaped(escaped_raw);
            curl_free(escaped_raw);

            std::string url = "https://api.search.brave.com/res/v1/web/search?q=" + escaped + "&count=" + std::to_string(count);

            HttpRequest req;
            req.url = url;
            req.timeout_seconds = 15;
            req.user_agent = "Prosophor/1.0";

            prosophor::HeaderList headers;
            headers.append("Accept: application/json");
            headers.append("Accept-Encoding: identity");
            headers.append(("X-Subscription-Token: " + std::string(brave_key)).c_str());
            req.headers = headers.get();

            HttpResponse resp = HttpClient::Instance().Get(req);
            if (resp.failed()) {
                throw std::runtime_error("Brave Search: " + resp.error_msg + " (HTTP " + std::to_string(resp.status_code) + ")");
            }

            auto j = nlohmann::json::parse(resp.body);
            result << "Search results (Brave): " << query << "\n\n";

            if (j.contains("web") && j["web"].contains("results")) {
                int i = 0;
                for (const auto& r : j["web"]["results"]) {
                    if (i >= count) break;
                    std::string title = r.value("title", "");
                    std::string result_url = r.value("url", "");
                    std::string desc = r.value("description", "");
                    result << (i + 1) << ". " << title << "\n   " << result_url << "\n   " << desc << "\n\n";
                    i++;
                }
                if (i > 0) return result.str();
            }
        } catch (const std::exception& e) {
            last_error = std::string("Brave: ") + e.what();
        }
    }

    // --- Tavily Search (API key required) ---
    if (tavily_key && *tavily_key) {
        try {
            nlohmann::json body = {
                {"api_key", tavily_key},
                {"query", query},
                {"max_results", count},
                {"search_depth", "basic"}
            };

            HttpRequest req;
            req.url = "https://api.tavily.com/search";
            req.body = body.dump();
            req.timeout_seconds = 15;

            prosophor::HeaderList headers;
            headers.append("Content-Type: application/json");
            req.headers = headers.get();

            HttpResponse resp = HttpClient::Instance().Post(req);
            if (resp.failed()) {
                throw std::runtime_error("Tavily: " + resp.error_msg + " (HTTP " + std::to_string(resp.status_code) + ")");
            }

            auto j = nlohmann::json::parse(resp.body);
            result.str("");
            result << "Search results (Tavily): " << query << "\n\n";

            if (j.contains("results")) {
                int i = 0;
                for (const auto& r : j["results"]) {
                    if (i >= count) break;
                    std::string title = r.value("title", "");
                    std::string url = r.value("url", "");
                    std::string desc = r.value("content", "");
                    result << (i + 1) << ". " << title << "\n   " << url << "\n   " << desc << "\n\n";
                    i++;
                }
                if (i > 0) return result.str();
            }
        } catch (const std::exception& e) {
            last_error = std::string("Tavily: ") + e.what();
        }
    }

    // --- DuckDuckGo HTML (no API key needed) ---
    try {
        char* encoded_raw = curl_easy_escape(nullptr, query.c_str(), (int)query.length());
        std::string encoded(encoded_raw);
        curl_free(encoded_raw);

        std::string url = "https://html.duckduckgo.com/html/?q=" + encoded;

        HttpRequest req;
        req.url = url;
        req.timeout_seconds = 15;
        req.user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";

        prosophor::HeaderList headers;
        headers.append("Accept: text/html");
        req.headers = headers.get();

        HttpResponse resp = HttpClient::Instance().Get(req);
        if (resp.success() && !resp.body.empty()) {
            result.str("");
            result << "Search results (DuckDuckGo): " << query << "\n\n";

            // Parse DuckDuckGo HTML results
            // Use simple string matching instead of regex (avoid encoding issues)
            std::vector<std::pair<std::string, std::string>> links;
            std::vector<std::string> snippets;

            // Find result links: class="result__a"
            size_t pos = 0;
            while ((pos = resp.body.find("class=\"result__a\"", pos)) != std::string::npos && (int)links.size() < count) {
                // Find href
                size_t href_start = resp.body.find("href=\"", pos);
                if (href_start == std::string::npos || href_start > pos + 200) {
                    pos++;
                    continue;
                }
                href_start += 6;
                size_t href_end = resp.body.find("\"", href_start);
                if (href_end == std::string::npos) {
                    pos++;
                    continue;
                }
                std::string link_url = resp.body.substr(href_start, href_end - href_start);

                // Find title (between > and </a>)
                size_t title_start = resp.body.find(">", href_end);
                if (title_start == std::string::npos || title_start > href_end + 300) {
                    pos++;
                    continue;
                }
                size_t title_end = resp.body.find("</a>", title_start);
                if (title_end == std::string::npos) {
                    pos++;
                    continue;
                }
                std::string title = html_to_text(resp.body.substr(title_start, title_end - title_start));
                links.emplace_back(link_url, title);
                pos = title_end;
            }

            // Find snippets: class="result__snippet"
            pos = 0;
            while ((pos = resp.body.find("class=\"result__snippet\"", pos)) != std::string::npos && (int)snippets.size() < count) {
                size_t start = resp.body.find(">", pos);
                if (start == std::string::npos || start > pos + 200) {
                    pos++;
                    continue;
                }
                size_t end = resp.body.find("</a>", start);
                if (end == std::string::npos) {
                    pos++;
                    continue;
                }
                snippets.push_back(html_to_text(resp.body.substr(start, end - start)));
                pos = end;
            }

            int n = std::min(count, (int)links.size());
            for (int i = 0; i < n; ++i) {
                // URL decode for DuckDuckGo redirect URLs
                std::string decoded_url = links[i].first;
                if (decoded_url.find("uddg=") != std::string::npos) {
                    size_t uddg_pos = decoded_url.find("uddg=") + 5;
                    size_t amp = decoded_url.find('&', uddg_pos);
                    std::string encoded_url = (amp != std::string::npos) ? decoded_url.substr(uddg_pos, amp - uddg_pos) : decoded_url.substr(uddg_pos);
                    decoded_url = url_decode(encoded_url);
                }
                result << (i + 1) << ". " << links[i].second << "\n   " << decoded_url;
                if (i < (int)snippets.size()) {
                    result << "\n   " << snippets[i];
                }
                result << "\n\n";
            }

            if (n > 0) {
                result << "[" << n << " results from DuckDuckGo]";
                return result.str();
            }
        }
    } catch (const std::exception& e) {
        last_error = std::string("DuckDuckGo: ") + e.what();
    }

    // No provider succeeded
    std::string error_msg = "web_search: no provider succeeded.";
    if (!last_error.empty()) {
        error_msg += " Last error: " + last_error;
    }
    error_msg += " Configure BRAVE_API_KEY, TAVILY_API_KEY, or use DuckDuckGo (no key required).";
    return error_msg;
}

std::string ToolRegistry::TokenCountTool(const nlohmann::json& params) {
    std::string text = params.value("text", "");
    std::string model = params.value("model", std::string("claude-3"));

    if (text.empty()) {
        throw std::runtime_error("Text is required");
    }

    int tokens = TokenCounter::CountTokens(text, model);
    int estimated = TokenCounter::EstimateTokens(text);

    std::ostringstream result;
    result << "Token counting for model: " << model << "\n\n";
    result << "Text length: " << text.length() << " characters\n";
    result << "Estimated tokens (char/4): " << estimated << "\n";
    result << "Counted tokens: " << tokens << "\n";

    return result.str();
}

std::string ToolRegistry::TokenUsageTool(const nlohmann::json& params) {
    std::string model = params.value("model", std::string(""));

    auto& tracker = TokenTracker::GetInstance();

    std::ostringstream result;
    result << "=== Token Usage Statistics ===\n\n";

    if (model.empty()) {
        // Show total stats
        TokenStats total = tracker.GetTotalStats();
        result << "Total Usage:\n";
        result << "  Prompt tokens: " << total.prompt_tokens << "\n";
        result << "  Completion tokens: " << total.completion_tokens << "\n";
        result << "  Total tokens: " << total.total_tokens << "\n";
        result << "  Estimated cost: $" << std::fixed << std::setprecision(4) << total.cost_usd << "\n\n";

        // Show per-model stats
        auto all_stats = tracker.GetAllStats();
        if (!all_stats.empty()) {
            result << "Per-Model Usage:\n";
            for (const auto& [m, stats] : all_stats) {
                result << "  " << m << ":\n";
                result << "    Prompt: " << stats.prompt_tokens << ", Completion: " << stats.completion_tokens;
                result << ", Total: " << stats.total_tokens;
                result << ", Cost: $" << std::fixed << std::setprecision(4) << stats.cost_usd << "\n";
            }
        }
    } else {
        // Show stats for specific model
        TokenStats stats = tracker.GetModelStats(model);
        if (stats.total_tokens == 0) {
            return "No token usage recorded for model: " + model;
        }
        result << "Model: " << model << "\n";
        result << "  Prompt tokens: " << stats.prompt_tokens << "\n";
        result << "  Completion tokens: " << stats.completion_tokens << "\n";
        result << "  Total tokens: " << stats.total_tokens << "\n";
        result << "  Estimated cost: $" << std::fixed << std::setprecision(4) << stats.cost_usd << "\n";
    }

    return result.str();
}

std::string ToolRegistry::McpListToolsTool(const nlohmann::json& params) {
    std::string server = params.value("server", std::string(""));

    auto& mcp_client = McpClient::GetInstance();
    std::ostringstream result;

    std::vector<McpTool> tools;
    if (server.empty()) {
        tools = mcp_client.GetAvailableTools();
    } else {
        tools = mcp_client.GetServerTools(server);
    }

    if (tools.empty()) {
        return "No MCP tools available";
    }

    result << "=== Available MCP Tools ===\n\n";
    for (const auto& tool : tools) {
        result << "  " << tool.name << " (server: " << tool.server_name << ")\n";
        if (!tool.description.empty()) {
            result << "    " << tool.description << "\n";
        }
        if (!tool.input_schema.is_null()) {
            result << "    Input schema: " << tool.input_schema.dump(2) << "\n";
        }
        result << "\n";
    }

    result << "[" << tools.size() << " tool(s) available]";
    return result.str();
}

std::string ToolRegistry::McpCallToolTool(const nlohmann::json& params) {
    std::string tool_name = params.value("tool_name", "");
    nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

    if (tool_name.empty()) {
        throw std::runtime_error("tool_name is required");
    }

    auto& mcp_client = McpClient::GetInstance();
    return mcp_client.CallTool(tool_name, arguments);
}

std::string ToolRegistry::GetMcpToolName(const McpTool& tool) const {
    return "mcp__" + tool.server_name + "__" + tool.name;
}

void ToolRegistry::RegisterMcpTools() {
    auto& mcp_client = McpClient::GetInstance();

    // Load configured servers and connect
    mcp_client.LoadServersFromFile();

    auto tools = mcp_client.GetAvailableTools();

    int registered_count = 0;
    for (const auto& mcp_tool : tools) {
        std::string registered_name = GetMcpToolName(mcp_tool);

        // Check if tool with same name already exists
        bool exists = false;
        for (const auto& schema : tool_schemas_) {
            if (schema.name == registered_name) {
                exists = true;
                break;
            }
        }

        if (exists) {
            continue;
        }

        // Register the MCP tool - capture tool details by value for later use
        std::string tool_name = mcp_tool.name;
        RegisterTool(
            registered_name,
            mcp_tool.description.empty() ? "MCP tool: " + mcp_tool.name : mcp_tool.description,
            mcp_tool.input_schema.is_null() ? nlohmann::json::object() : mcp_tool.input_schema,
            [tool_name](const nlohmann::json& p) {
                return McpClient::GetInstance().CallTool(tool_name, p);
            }
        );

        registered_count++;
        LOG_INFO("Registered MCP tool: {} -> {}", registered_name, mcp_tool.name);
    }

    LOG_INFO("Registered {} MCP tools", registered_count);
}

std::vector<std::string> ToolRegistry::GetRegisteredMcpServers() const {
    std::vector<std::string> servers;
    for (const auto& schema : tool_schemas_) {
        if (schema.name.find("mcp__") == 0) {
            // Extract server name: mcp__<server>__<tool>
            size_t first_sep = schema.name.find("__", 5);
            if (first_sep != std::string::npos) {
                std::string server = schema.name.substr(5, first_sep - 5);
                if (std::find(servers.begin(), servers.end(), server) == servers.end()) {
                    servers.push_back(server);
                }
            }
        }
    }
    return servers;
}

bool ToolRegistry::UnregisterMcpServer(const std::string& server_name) {
    std::string prefix = "mcp__" + server_name + "__";
    std::vector<std::string> tools_to_remove;

    for (const auto& schema : tool_schemas_) {
        if (schema.name.find(prefix) == 0) {
            tools_to_remove.push_back(schema.name);
        }
    }

    for (const auto& tool_name : tools_to_remove) {
        tools_.erase(tool_name);
    }

    // Remove from schemas
    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [&prefix](const ToolsSchema& schema) {
                return schema.name.find(prefix) == 0;
            }),
        tool_schemas_.end()
    );

    LOG_INFO("Unregistered {} MCP tools from server: {}", tools_to_remove.size(), server_name);
    return !tools_to_remove.empty();
}

std::string ToolRegistry::McpListResourcesTool(const nlohmann::json& /* params */) {
    auto& mcp_client = McpClient::GetInstance();
    auto resources = mcp_client.GetAvailableResources();

    std::ostringstream result;
    if (resources.empty()) {
        return "No MCP resources available";
    }

    result << "=== Available MCP Resources ===\n\n";
    for (const auto& res : resources) {
        result << "  " << res.name << "\n";
        result << "    URI: " << res.uri << "\n";
        if (!res.description.empty()) {
            result << "    " << res.description << "\n";
        }
        result << "    MIME type: " << res.mime_type << "\n\n";
    }

    result << "[" << resources.size() << " resource(s) available]";
    return result.str();
}

std::string ToolRegistry::McpReadResourceTool(const nlohmann::json& params) {
    std::string uri = params.value("uri", "");

    if (uri.empty()) {
        throw std::runtime_error("uri is required");
    }

    auto& mcp_client = McpClient::GetInstance();
    return mcp_client.ReadResource(uri);
}

// ==================== Git Tools ====================

std::string ToolRegistry::GitStatusTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    if (path.empty()) {
        path = workspace_path_;
    }

    std::string cmd = "cd \"" + path + "\" && git status";
    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        return FormatCommandResult(result, status, "Error: Not a git repository or git not available");
    }
    return FormatCommandResult(result, status);
}

std::string ToolRegistry::GitDiffTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    bool cached = params.value("cached", false);

    if (path.empty()) {
        path = workspace_path_;
    }

    std::string cmd = "cd \"" + path + "\" && git diff ";
    if (cached) {
        cmd += "--cached ";
    }

    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        return FormatCommandResult(result, status, "Error: Failed to get git diff");
    }

    if (result.empty()) {
        return cached ? "No staged changes.[Exit code: 0]" : "No working directory changes.[Exit code: 0]";
    }

    return FormatCommandResult(result, status);
}

std::string ToolRegistry::GitLogTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    int max_count = params.value("max_count", 10);

    if (max_count <= 0) max_count = 10;
    if (max_count > 50) max_count = 50;

    if (path.empty()) {
        path = workspace_path_;
    }

    std::string cmd = "cd \"" + path + "\" && git log --oneline -n " + std::to_string(max_count);
    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        return FormatCommandResult(result, status, "Error: Failed to execute git log");
    }

    int count = static_cast<int>(std::count(result.begin(), result.end(), '\n'));
    if (count == 0) {
        return "No commit history found.[Exit code: 0]";
    }

    return result + "\n[" + std::to_string(count) + " commit(s) shown][Exit code: " + std::to_string(status) + "]";
}

std::string ToolRegistry::GitCommitTool(const nlohmann::json& params) {
    std::string message = params.value("message", "");
    bool do_all = params.value("all", false);

    if (message.empty()) {
        throw std::runtime_error("Commit message is required");
    }

    // Escape message for shell
    std::string escaped_msg;
    for (char c : message) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            escaped_msg += '\\';
        }
        escaped_msg += c;
    }

    std::string cmd = "git ";
    if (do_all) {
        cmd += "commit -a ";
    } else {
        cmd += "commit ";
    }
    cmd += "-m \"" + escaped_msg + "\"";

    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        if (result.find("nothing to commit") != std::string::npos) {
            return "Nothing to commit, working tree clean.[Exit code: " + std::to_string(status) + "]";
        }
        return FormatCommandResult(result, status, "Error: ");
    }

    return FormatCommandResult(result, status);
}

std::string ToolRegistry::GitAddTool(const nlohmann::json& params) {
    std::vector<std::string> files;
    if (params.contains("files") && params["files"].is_array()) {
        files = params["files"].get<std::vector<std::string>>();
    }

    std::string cmd = "git add ";
    if (files.empty()) {
        cmd += "-A";
    } else {
        for (const auto& f : files) {
            cmd += "\"" + f + "\" ";
        }
    }

    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        return FormatCommandResult(result, status, "Error: ");
    }

    if (files.empty()) {
        return "All changes staged for commit.[Exit code: " + std::to_string(status) + "]";
    }
    return "Staged " + std::to_string(files.size()) + " file(s) for commit.[Exit code: " + std::to_string(status) + "]";
}

std::string ToolRegistry::GitBranchTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    std::string create_branch = params.value("create", "");
    bool checkout = params.value("checkout", false);

    if (path.empty()) {
        path = workspace_path_;
    }

    std::string result;

    // Create new branch if requested
    if (!create_branch.empty()) {
        std::string cmd = "cd \"" + path + "\" && git ";
        if (checkout) {
            cmd += "checkout -b \"" + create_branch + "\"";
        } else {
            cmd += "branch \"" + create_branch + "\"";
        }

        auto [branch_result, status] = ExecuteCommand(cmd);
        result += branch_result + "\n";
    }

    // List branches
    std::string cmd = "cd \"" + path + "\" && git branch";
    auto [list_result, status] = ExecuteCommand(cmd);

    int count = static_cast<int>(std::count(list_result.begin(), list_result.end(), '\n'));
    result += list_result;

    if (count == 0) {
        return "No branches found or not a git repository.";
    }

    result += "\n[" + std::to_string(count) + " branch(es)]";
    return result;
}

// ==================== Memory Tools ====================

std::string ToolRegistry::MemorySearchTool(const nlohmann::json& params) {
    std::string query = params.value("query", "");
    int max_results = params.value("max_results", 10);

    if (query.empty()) {
        throw std::runtime_error("query is required");
    }

    // Convert query to lowercase for case-insensitive matching
    std::string query_lower = query;
    std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Collect tokens from query for matching
    std::vector<std::string> tokens;
    std::istringstream iss(query_lower);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }

    if (tokens.empty()) {
        throw std::runtime_error("query is required");
    }

    struct SearchResult {
        std::string source;
        std::string content;
        int score;
        int line_number;
    };

    std::vector<SearchResult> results;

    // Search workspace directory for text files
    namespace fs = std::filesystem;
    fs::path ws(workspace_path_);

    if (fs::exists(ws) && fs::is_directory(ws)) {
        for (auto& entry : fs::recursive_directory_iterator(ws)) {
            if (!entry.is_regular_file()) continue;

            auto ext = entry.path().extension().string();
            // Only search text-based files
            bool is_text = (ext == ".md" || ext == ".txt" || ext == ".cc" ||
                           ext == ".h" || ext == ".cpp" || ext == ".hpp" ||
                           ext == ".json" || ext == ".yaml" || ext == ".yml" ||
                           ext == ".py" || ext == ".js" || ext == ".ts" ||
                           ext == ".rs" || ext == ".go" || ext == ".cmake" ||
                           ext == ".sh" || ext == ".ini" || ext == ".cfg");
            if (!is_text) continue;

            // Skip binary or very large files (>500KB)
            if (entry.file_size() > 512000) continue;

            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            std::string line;
            int line_num = 0;
            while (std::getline(file, line)) {
                line_num++;
                std::string line_lower = line;
                std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                // Count matching tokens
                int match_count = 0;
                for (const auto& t : tokens) {
                    if (line_lower.find(t) != std::string::npos) {
                        match_count++;
                    }
                }

                if (match_count > 0) {
                    SearchResult sr;
                    sr.source = fs::relative(entry.path(), ws).string();
                    sr.line_number = line_num;

                    // Trim line for display
                    if (line.size() > 200) {
                        sr.content = line.substr(0, 200) + "...";
                    } else {
                        sr.content = line;
                    }

                    sr.score = match_count;
                    results.push_back(sr);

                    // Limit per-file results to avoid flooding
                    if (results.size() >= (size_t)max_results * 3) break;
                }
            }
        }
    }

    // Sort by score descending
    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.score > b.score;
              });

    // Trim to max_results
    if (results.size() > (size_t)max_results) {
        results.resize(max_results);
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : results) {
        nlohmann::json entry;
        entry["source"] = r.source;
        entry["content"] = r.content;
        entry["score"] = r.score;
        entry["lineNumber"] = r.line_number;
        arr.push_back(entry);
    }

    nlohmann::json result;
    result["results"] = arr;
    result["count"] = arr.size();
    result["query"] = query;
    return result.dump(2);
}

std::string ToolRegistry::MemoryGetTool(const nlohmann::json& params) {
    std::string rel_path = params.value("path", "");
    if (rel_path.empty()) {
        throw std::runtime_error("path is required");
    }

    namespace fs = std::filesystem;
    fs::path ws(workspace_path_);
    fs::path full_path = ws / rel_path;

    // Security: resolve and verify path stays within workspace
    std::error_code ec;
    auto canonical_full = fs::weakly_canonical(full_path, ec);
    if (ec) {
        throw std::runtime_error("Invalid path: " + rel_path);
    }

    auto canonical_ws = fs::weakly_canonical(ws, ec);
    if (ec) {
        throw std::runtime_error("Workspace not accessible: " + workspace_path_);
    }

    // Check prefix match (path must be under workspace)
    auto ws_str = canonical_ws.string();
    auto full_str = canonical_full.string();
    if (full_str.find(ws_str) != 0) {
        throw std::runtime_error("Access denied: path outside workspace");
    }

    if (!fs::exists(full_path)) {
        throw std::runtime_error("File not found: " + rel_path);
    }

    if (!fs::is_regular_file(full_path)) {
        throw std::runtime_error("Not a file: " + rel_path);
    }

    std::ifstream f(full_path);
    if (!f) {
        throw std::runtime_error("Cannot read: " + rel_path);
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    nlohmann::json result;
    result["path"] = rel_path;
    result["content"] = content;
    return result.dump(2);
}

std::string ToolRegistry::ApplyPatchTool(const nlohmann::json& params) {
    std::string patch = params.value("patch", "");
    if (patch.empty()) {
        throw std::runtime_error("patch is required");
    }

    // Find Begin/End markers
    const std::string kBegin = "*** Begin Patch";
    const std::string kEnd = "*** End Patch";
    size_t begin_pos = patch.find(kBegin);
    size_t end_pos = patch.find(kEnd);

    if (begin_pos == std::string::npos) {
        throw std::runtime_error("Missing '*** Begin Patch' marker");
    }

    std::string body;
    if (end_pos != std::string::npos) {
        body = patch.substr(begin_pos + kBegin.size(), end_pos - begin_pos - kBegin.size());
    } else {
        body = patch.substr(begin_pos + kBegin.size());
    }

    // Split into lines
    std::vector<std::string> lines;
    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }

    namespace fs = std::filesystem;
    int applied = 0;
    std::string current_file;
    enum class FileOp { None, Add, Update, Delete } op = FileOp::None;
    std::vector<std::string> add_content;
    std::vector<std::string> diff_hunks;

    auto flush_file = [&]() {
        if (current_file.empty()) return;

        fs::path fpath(current_file);

        if (op == FileOp::Add) {
            fs::create_directories(fpath.parent_path());
            std::ofstream f(fpath);
            for (const auto& l : add_content) {
                f << l << "\n";
            }
            applied++;
        } else if (op == FileOp::Delete) {
            if (fs::exists(fpath)) fs::remove(fpath);
            applied++;
        } else if (op == FileOp::Update && !diff_hunks.empty()) {
            if (!fs::exists(fpath)) {
                LOG_WARN("ApplyPatch: file not found for update: {}", current_file);
                current_file.clear();
                op = FileOp::None;
                diff_hunks.clear();
                return;
            }

            std::ifstream f(fpath);
            std::vector<std::string> file_lines;
            std::string fl;
            while (std::getline(f, fl)) {
                if (!fl.empty() && fl.back() == '\r') fl.pop_back();
                file_lines.push_back(fl);
            }
            f.close();

            // Apply hunks (simple line-based application)
            std::vector<std::string> result_lines = file_lines;
            int line_offset = 0;
            size_t i = 0;
            while (i < diff_hunks.size()) {
                std::string& hl = diff_hunks[i];
                if (hl.size() >= 2 && hl.substr(0, 2) == "@@") {
                    // Parse @@ -old_start,old_count +new_start,new_count @@
                    int old_start = 0;
                    int old_count = 0;
                    sscanf(hl.c_str(), "@@ -%d,%d", &old_start, &old_count);
                    int apply_at = old_start - 1 + line_offset;

                    // Collect hunk lines
                    std::vector<std::string> removed, added;
                    size_t j = i + 1;
                    while (j < diff_hunks.size() && diff_hunks[j].size() >= 2 &&
                           diff_hunks[j].substr(0, 2) != "@@") {
                        char ch = diff_hunks[j][0];
                        std::string content = diff_hunks[j].substr(1);
                        if (ch == '-') removed.push_back(content);
                        else if (ch == '+') added.push_back(content);
                        j++;
                    }

                    // Splice: replace removed lines with added lines
                    if (apply_at >= 0 &&
                        apply_at <= static_cast<int>(result_lines.size())) {
                        result_lines.erase(
                            result_lines.begin() + apply_at,
                            result_lines.begin() + apply_at +
                                static_cast<int>(removed.size()));
                        result_lines.insert(result_lines.begin() + apply_at,
                                            added.begin(), added.end());
                        line_offset += static_cast<int>(added.size()) -
                                       static_cast<int>(removed.size());
                    }
                    i = j;
                } else {
                    i++;
                }
            }

            std::ofstream out(fpath);
            for (const auto& l : result_lines) {
                out << l << "\n";
            }
            applied++;
        }

        current_file.clear();
        op = FileOp::None;
        add_content.clear();
        diff_hunks.clear();
    };

    for (const auto& l : lines) {
        if (l.substr(0, 16) == "*** Add File: ") {
            flush_file();
            current_file = l.substr(14);
            op = FileOp::Add;
        } else if (l.substr(0, 19) == "*** Update File: ") {
            flush_file();
            current_file = l.substr(17);
            op = FileOp::Update;
        } else if (l.substr(0, 19) == "*** Delete File: ") {
            flush_file();
            current_file = l.substr(17);
            op = FileOp::Delete;
        } else if (op == FileOp::Add) {
            add_content.push_back(l);
        } else if (op == FileOp::Update) {
            diff_hunks.push_back(l);
        }
    }
    flush_file();

    nlohmann::json result;
    result["ok"] = true;
    result["applied"] = applied;
    result["message"] = "Applied patch: " + std::to_string(applied) + " file(s) modified";
    return result.dump(2);
}

// ==================== Background Process Tool ====================

std::string ToolRegistry::BackgroundRunTool(const nlohmann::json& params) {
    std::string action = params.value("action", "run");
    return prosophor::BackgroundRunTool::GetInstance().Execute(action, params);
}

}  // namespace prosophor
