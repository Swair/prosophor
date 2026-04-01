// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace aicode {

/// MCP Tool definition
struct McpTool {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
    std::string server_name;  // MCP server that provides this tool
};

/// MCP Resource definition
struct McpResource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
};

/// MCP Prompt definition
struct McpPrompt {
    std::string name;
    std::string description;
    std::vector<std::string> arguments;
};

/// MCP Server configuration
struct McpServerConfig {
    std::string name;
    std::string type;  // "stdio" | "sse" | "websocket"
    std::string command;  // For stdio: command to run
    std::vector<std::string> args;  // Command arguments
    std::string url;  // For SSE/WebSocket
    nlohmann::json env;  // Environment variables
    bool enabled = true;
};

/// MCP Client for connecting to MCP servers
class McpClient {
public:
    static McpClient& GetInstance();

    /// Initialize MCP client with configuration
    void Initialize(const std::vector<McpServerConfig>& servers);

    /// Connect to a specific MCP server
    bool ConnectToServer(const McpServerConfig& config);

    /// Disconnect from a server
    void DisconnectFromServer(const std::string& server_name);

    /// Check if connected to a server
    bool IsConnected(const std::string& server_name) const;

    /// Get all available tools from all servers
    std::vector<McpTool> GetAvailableTools() const;

    /// Get tools from a specific server
    std::vector<McpTool> GetServerTools(const std::string& server_name) const;

    /// Get all resources
    std::vector<McpResource> GetAvailableResources() const;

    /// Get all prompts
    std::vector<McpPrompt> GetAvailablePrompts() const;

    /// Call a tool on an MCP server
    std::string CallTool(const std::string& tool_name, const nlohmann::json& arguments);

    /// Read a resource
    std::string ReadResource(const std::string& uri);

    /// Get a prompt
    std::string GetPrompt(const std::string& prompt_name, const nlohmann::json& args);

    /// Shutdown all connections
    void Shutdown();

    /// Server management (for /mcp add/remove)
    std::vector<McpServerConfig> GetConfiguredServers() const;
    bool AddServer(const McpServerConfig& config);
    bool RemoveServer(const std::string& server_name);
    std::string GetConfigFilePath() const;
    bool LoadServersFromFile();
    bool SaveServersToFile();

private:
    McpClient() = default;
    ~McpClient();

    struct ServerConnection {
        std::string name;
        std::string type;
        void* process_handle = nullptr;  // For stdio
        int stdin_fd = -1;  // Pipe fd for writing to server stdin
        int stdout_fd = -1;  // Pipe fd for reading from server stdout
        std::string read_buffer;
        int request_id = 0;
        std::vector<McpTool> tools;
        std::vector<McpResource> resources;
        std::vector<McpPrompt> prompts;
    };

    std::unordered_map<std::string, std::unique_ptr<ServerConnection>> servers_;

    /// Send JSON-RPC request to a server
    nlohmann::json SendRequest(ServerConnection& conn, const std::string& method,
                                const nlohmann::json& params = nlohmann::json::object());

    /// Parse incoming JSON-RPC response
    nlohmann::json ParseResponse(const std::string& response);

    /// Read available data from server
    std::string ReadFromServer(ServerConnection& conn);

    /// Write data to server
    void WriteToServer(ServerConnection& conn, const std::string& data);
};

}  // namespace aicode
