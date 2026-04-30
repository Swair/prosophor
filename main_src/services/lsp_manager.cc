// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "services/lsp_manager.h"

#include <sstream>
#include <cstdlib>

#include "common/log_wrapper.h"

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#else
#include <io.h>
#define close _close
#define write _write
#define read _read
#endif

namespace prosophor {

LspManager& LspManager::GetInstance() {
    static LspManager instance;
    return instance;
}

void LspManager::Initialize() {
    LOG_DEBUG("LspManager initialized");

    // Register common LSP servers
    // TypeScript
    {
        LspServerConfig config;
        config.name = "typescript";
        config.command = "typescript-language-server";
        config.args = {"--stdio"};
        config.file_patterns = {"*.ts", "*.tsx", "*.js", "*.jsx"};
        config.initialization_options["provideFormatter"] = true;
        registered_configs_.push_back(config);
    }

    // Python (Pylsp)
    {
        LspServerConfig config;
        config.name = "python";
        config.command = "pylsp";
        config.args = {};
        config.file_patterns = {"*.py"};
        registered_configs_.push_back(config);
    }

    // Python (Pyright)
    {
        LspServerConfig config;
        config.name = "pyright";
        config.command = "pyright-langserver";
        config.args = {"--stdio"};
        config.file_patterns = {"*.py"};
        registered_configs_.push_back(config);
    }

    // C++ (clangd)
    {
        LspServerConfig config;
        config.name = "clangd";
        config.command = "clangd";
        config.args = {};
        config.file_patterns = {"*.c", "*.cc", "*.cpp", "*.h", "*.hpp"};
        registered_configs_.push_back(config);
    }

    // Rust (rust-analyzer)
    {
        LspServerConfig config;
        config.name = "rust";
        config.command = "rust-analyzer";
        config.args = {};
        config.file_patterns = {"*.rs"};
        registered_configs_.push_back(config);
    }

    // Go (gopls)
    {
        LspServerConfig config;
        config.name = "go";
        config.command = "gopls";
        config.args = {};
        config.file_patterns = {"*.go"};
        registered_configs_.push_back(config);
    }
}

void LspManager::RegisterServer(const LspServerConfig& config) {
    registered_configs_.push_back(config);
    LOG_INFO("Registered LSP server: {}", config.name);
}

bool LspManager::StartServerForFile(const std::string& filepath) {
#ifdef _WIN32
    LOG_WARN("LSP server management is not supported on Windows");
    return false;
#else
    auto* server = FindServerForFile(filepath);
    if (server) {
        return true;  // Already running
    }

    // Find matching config
    for (auto& config : registered_configs_) {
        for (const auto& pattern : config.file_patterns) {
            // Simple pattern matching
            if (filepath.size() >= pattern.size() - 1 &&
                filepath.compare(filepath.size() - pattern.size() + 1,
                                 std::string::npos,
                                 pattern.substr(1)) == 0) {
                // Start the server
                ServerInstance instance;
                instance.config = config;
                instance.root_path = config.root_path;

                // Create pipes for communication
                int stdin_pipe[2], stdout_pipe[2];
                if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
                    LOG_ERROR("Failed to create pipes for LSP server");
                    return false;
                }

                pid_t pid = fork();
                if (pid == 0) {
                    // Child process
                    close(stdin_pipe[1]);
                    close(stdout_pipe[0]);
                    dup2(stdin_pipe[0], STDIN_FILENO);
                    dup2(stdout_pipe[1], STDOUT_FILENO);
                    close(stdin_pipe[0]);
                    close(stdout_pipe[1]);

                    // Execute server
                    std::vector<char*> args;
                    args.push_back(const_cast<char*>(config.command.c_str()));
                    for (auto& arg : config.args) {
                        args.push_back(const_cast<char*>(arg.c_str()));
                    }
                    args.push_back(nullptr);

                    execvp(config.command.c_str(), args.data());
                    _exit(1);
                } else if (pid > 0) {
                    // Parent process
                    close(stdin_pipe[0]);
                    close(stdout_pipe[1]);
                    instance.process = reinterpret_cast<void*>(static_cast<size_t>(pid));
                    instance.stdin_fd = stdin_pipe[1];
                    instance.stdout_fd = stdout_pipe[0];

                    servers_[config.name] = std::move(instance);
                    LOG_INFO("Started LSP server: {} (PID: {})", config.name, pid);

                    // Initialize the server
                    InitializeServer(servers_[config.name]);

                    return true;
                } else {
                    LOG_ERROR("Failed to fork LSP server process");
                    return false;
                }
            }
        }
    }

    LOG_DEBUG("No LSP server found for file: {}", filepath);
    return false;
#endif
}

LspManager::ServerInstance* LspManager::FindServerForFile(const std::string& filepath) {
    for (auto& [name, server] : servers_) {
        for (const auto& pattern : server.config.file_patterns) {
            if (filepath.size() >= pattern.size() - 1 &&
                filepath.compare(filepath.size() - pattern.size() + 1,
                                 std::string::npos,
                                 pattern.substr(1)) == 0) {
                return &server;
            }
        }
    }
    return nullptr;
}

bool LspManager::InitializeServer(ServerInstance& server) {
    if (server.initialized) {
        return true;
    }

    nlohmann::json params = nlohmann::json::object();
#ifndef _WIN32
    params["processId"] = getpid();
#else
    params["processId"] = 0;  // Windows process ID placeholder
#endif
    params["rootUri"] = server.config.root_path.empty() ? nullptr : server.config.root_path;
    params["capabilities"] = nlohmann::json::object();
    params["initializationOptions"] = server.config.initialization_options;

    SendRequest(server, "initialize", params);
    SendNotification(server, "initialized", nlohmann::json::object());

    server.initialized = true;
    LOG_INFO("Initialized LSP server: {}", server.config.name);
    return true;
}

nlohmann::json LspManager::SendRequest(ServerInstance& server,
                                        const std::string& method,
                                        const nlohmann::json& params) {
    server.request_id++;

    nlohmann::json rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["id"] = server.request_id;
    rpc["method"] = method;
    rpc["params"] = params;

    std::string body = rpc.dump();
    std::ostringstream header;
    header << "Content-Length: " << body.size() << "\r\n\r\n";

    std::string message = header.str() + body;
    write(server.stdin_fd, message.c_str(), message.size());

    return ReadResponse(server);
}

void LspManager::SendNotification(ServerInstance& server,
                                   const std::string& method,
                                   const nlohmann::json& params) {
    nlohmann::json rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["method"] = method;
    rpc["params"] = params;

    std::string body = rpc.dump();
    std::ostringstream header;
    header << "Content-Length: " << body.size() << "\r\n\r\n";

    std::string message = header.str() + body;
    write(server.stdin_fd, message.c_str(), message.size());
}

std::string LspManager::ReadResponse(ServerInstance& server) {
    char buffer[4096];
    ssize_t n;

    while ((n = read(server.stdout_fd, buffer, sizeof(buffer))) > 0) {
        server.read_buffer.append(buffer, n);

        // Look for Content-Length header
        size_t header_end = server.read_buffer.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            std::string header = server.read_buffer.substr(0, header_end);
            size_t content_length_pos = header.find("Content-Length: ");
            if (content_length_pos != std::string::npos) {
                size_t content_length_end = header.find("\r\n", content_length_pos);
                std::string content_length_str = header.substr(
                    content_length_pos + 16,
                    content_length_end - content_length_pos - 16
                );
                int content_length = std::stoi(content_length_str);

                size_t content_start = header_end + 4;
                if (server.read_buffer.size() >= content_start + content_length) {
                    std::string content = server.read_buffer.substr(
                        content_start, content_length);
                    server.read_buffer = server.read_buffer.substr(
                        content_start + content_length);
                    return content;
                }
            }
        }
    }

    return "";
}

std::vector<Diagnostic> LspManager::GetDiagnostics(const std::string& uri) {
    for (auto& [name, server] : servers_) {
        auto it = server.diagnostics.find(uri);
        if (it != server.diagnostics.end()) {
            return it->second;
        }
    }
    return {};
}

std::vector<Diagnostic> LspManager::GetAllDiagnostics() const {
    std::vector<Diagnostic> all;
    for (const auto& [name, server] : servers_) {
        for (const auto& [uri, diags] : server.diagnostics) {
            all.insert(all.end(), diags.begin(), diags.end());
        }
    }
    return all;
}

std::vector<Symbol> LspManager::GoToDefinition(const std::string& uri,
                                                            int line, int character) {
    auto* server = FindServerForFile(uri);
    if (!server) {
        return {};
    }

    nlohmann::json params;
    params["textDocument"]["uri"] = uri;
    params["position"]["line"] = line;
    params["position"]["character"] = character;

    auto result = SendRequest(*server, "textDocument/definition", params);

    std::vector<Symbol> symbols;
    if (result.contains("result")) {
        if (result["result"].is_array()) {
            for (const auto& loc : result["result"]) {
                Symbol sym;
                sym.uri = loc.value("uri", "");
                sym.line = loc.value("range", nlohmann::json::object())
                           .value("start", nlohmann::json::object())
                           .value("line", 0);
                symbols.push_back(sym);
            }
        }
    }

    return symbols;
}

std::vector<Symbol> LspManager::FindReferences(const std::string& uri,
                                                            int line, int character) {
    auto* server = FindServerForFile(uri);
    if (!server) {
        return {};
    }

    nlohmann::json params;
    params["textDocument"]["uri"] = uri;
    params["position"]["line"] = line;
    params["position"]["character"] = character;
    params["context"]["includeDeclaration"] = true;

    auto result = SendRequest(*server, "textDocument/references", params);

    std::vector<Symbol> symbols;
    if (result.contains("result") && result["result"].is_array()) {
        for (const auto& loc : result["result"]) {
            Symbol sym;
            sym.uri = loc.value("uri", "");
            sym.line = loc.value("range", nlohmann::json::object())
                       .value("start", nlohmann::json::object())
                       .value("line", 0);
            symbols.push_back(sym);
        }
    }

    return symbols;
}

std::string LspManager::GetHover(const std::string& uri, int line, int character) {
    auto* server = FindServerForFile(uri);
    if (!server) {
        return "";
    }

    nlohmann::json params;
    params["textDocument"]["uri"] = uri;
    params["position"]["line"] = line;
    params["position"]["character"] = character;

    auto result = SendRequest(*server, "textDocument/hover", params);

    if (result.contains("result") && result["result"].contains("contents")) {
        auto& contents = result["result"]["contents"];
        if (contents.is_string()) {
            return contents.get<std::string>();
        }
        if (contents.contains("value")) {
            return contents["value"].get<std::string>();
        }
    }

    return "";
}

std::vector<Symbol> LspManager::GetDocumentSymbols(const std::string& uri) {
    auto* server = FindServerForFile(uri);
    if (!server) {
        return {};
    }

    nlohmann::json params;
    params["textDocument"]["uri"] = uri;

    auto result = SendRequest(*server, "textDocument/documentSymbol", params);

    std::vector<Symbol> symbols;
    if (result.contains("result") && result["result"].is_array()) {
        for (const auto& sym : result["result"]) {
            Symbol s;
            s.name = sym.value("name", "");
            s.kind = sym.value("kind", "");
            s.uri = uri;
            symbols.push_back(s);
        }
    }

    return symbols;
}

std::vector<Symbol> LspManager::WorkspaceSymbols(const std::string& query) {
    std::vector<Symbol> symbols;

    for (auto& [name, server] : servers_) {
        nlohmann::json params;
        params["query"] = query;

        auto result = SendRequest(server, "workspace/symbol", params);

        if (result.contains("result") && result["result"].is_array()) {
            for (const auto& sym : result["result"]) {
                Symbol s;
                s.name = sym.value("name", "");
                s.kind = sym.value("kind", "");
                s.uri = sym.value("location", nlohmann::json::object())
                        .value("uri", "");
                symbols.push_back(s);
            }
        }
    }

    return symbols;
}

std::string LspManager::FormatDocument(const std::string& uri) {
    auto* server = FindServerForFile(uri);
    if (!server) {
        return "";
    }

    nlohmann::json params;
    params["textDocument"]["uri"] = uri;

    auto result = SendRequest(*server, "textDocument/formatting", params);

    if (result.contains("result") && result["result"].is_array()) {
        for (const auto& edit : result["result"]) {
            if (edit.contains("newText")) {
                return edit["newText"].get<std::string>();
            }
        }
    }

    return "";
}

bool LspManager::IsLspAvailable(const std::string& filepath) const {
    for (const auto& config : registered_configs_) {
        for (const auto& pattern : config.file_patterns) {
            if (filepath.size() >= pattern.size() - 1 &&
                filepath.compare(filepath.size() - pattern.size() + 1,
                                 std::string::npos,
                                 pattern.substr(1)) == 0) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> LspManager::GetRegisteredServers() const {
    std::vector<std::string> names;
    for (const auto& config : registered_configs_) {
        names.push_back(config.name);
    }
    return names;
}

void LspManager::ShutdownAll() {
    for (auto& [name, server] : servers_) {
        SendRequest(server, "shutdown", nlohmann::json::object());
        SendNotification(server, "exit", nlohmann::json::object());

#ifndef _WIN32
        if (server.process) {
            pid_t pid = static_cast<pid_t>(reinterpret_cast<size_t>(server.process));
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, WNOHANG);
        }
#endif

        if (server.stdin_fd > 0) close(server.stdin_fd);
        if (server.stdout_fd > 0) close(server.stdout_fd);
    }

    servers_.clear();
    LOG_INFO("All LSP servers shut down");
}

}  // namespace prosophor
