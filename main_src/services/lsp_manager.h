// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>

namespace aicode {

/// LSP Server configuration
struct LspServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::string root_path;
    nlohmann::json initialization_options;
    std::vector<std::string> file_patterns;  // File patterns this server handles
};

/// Diagnostic severity
enum class DiagnosticSeverity {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4
};

/// LSP Diagnostic
struct Diagnostic {
    std::string uri;
    int line = 0;
    int character = 0;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::string source;
    std::string code;
};

/// LSP Symbol
struct Symbol {
    std::string name;
    std::string kind;
    std::string uri;
    int line = 0;
    int character = 0;
};

/// LSP Manager for language server protocol
class LspManager {
public:
    static LspManager& GetInstance();

    /// Initialize LSP manager
    void Initialize();

    /// Register an LSP server configuration
    void RegisterServer(const LspServerConfig& config);

    /// Start LSP server for a file
    bool StartServerForFile(const std::string& filepath);

    /// Stop LSP server
    bool StopServer(const std::string& server_name);

    /// Get diagnostics for a file
    std::vector<Diagnostic> GetDiagnostics(const std::string& uri);

    /// Get all diagnostics
    std::vector<Diagnostic> GetAllDiagnostics() const;

    /// Go to definition
    std::vector<Symbol> GoToDefinition(const std::string& uri, int line, int character);

    /// Find references
    std::vector<Symbol> FindReferences(const std::string& uri, int line, int character);

    /// Get hover information
    std::string GetHover(const std::string& uri, int line, int character);

    /// Get document symbols
    std::vector<Symbol> GetDocumentSymbols(const std::string& uri);

    /// Get workspace symbols
    std::vector<Symbol> WorkspaceSymbols(const std::string& query);

    /// Format document
    std::string FormatDocument(const std::string& uri);

    /// Check if LSP is available for a file
    bool IsLspAvailable(const std::string& filepath) const;

    /// Get registered server names
    std::vector<std::string> GetRegisteredServers() const;

    /// Shutdown all servers
    void ShutdownAll();

private:
    LspManager() = default;
    ~LspManager() = default;

    struct ServerInstance {
        LspServerConfig config;
        std::string root_path;  // Project root path
        void* process = nullptr;  // Process handle
        int stdin_fd = -1;
        int stdout_fd = -1;
        int request_id = 0;
        bool initialized = false;
        std::string read_buffer;
        std::unordered_map<std::string, std::vector<Diagnostic>> diagnostics;
    };

    std::unordered_map<std::string, ServerInstance> servers_;
    std::vector<LspServerConfig> registered_configs_;

    /// Find matching server for file
    ServerInstance* FindServerForFile(const std::string& filepath);

    /// Send JSON-RPC request
    nlohmann::json SendRequest(ServerInstance& server,
                                const std::string& method,
                                const nlohmann::json& params);

    /// Send notification
    void SendNotification(ServerInstance& server,
                          const std::string& method,
                          const nlohmann::json& params);

    /// Read response from server
    std::string ReadResponse(ServerInstance& server);

    /// Initialize server
    bool InitializeServer(ServerInstance& server);

    /// Open document in server
    void OpenDocument(ServerInstance& server, const std::string& uri, const std::string& text);

    /// Close document in server
    void CloseDocument(ServerInstance& server, const std::string& uri);
};

}  // namespace aicode
