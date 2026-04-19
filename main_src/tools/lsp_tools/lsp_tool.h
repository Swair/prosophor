// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "services/lsp_manager.h"

namespace aicode {

/// LSP Tool implementation
class LspTool {
public:
    static LspTool& GetInstance();

    /// Get diagnostics for a file
    /// @param params {"uri": "file://...", "severity": "error|warning|info"}
    std::string Diagnostics(const nlohmann::json& params);

    /// Go to definition
    /// @param params {"uri": "file://...", "line": 10, "character": 5}
    std::string GoToDefinition(const nlohmann::json& params);

    /// Find references
    /// @param params {"uri": "file://...", "line": 10, "character": 5, "include_declaration": true}
    std::string FindReferences(const nlohmann::json& params);

    /// Get hover information
    /// @param params {"uri": "file://...", "line": 10, "character": 5}
    std::string GetHover(const nlohmann::json& params);

    /// Get document symbols
    /// @param params {"uri": "file://..."}
    std::string GetDocumentSymbols(const nlohmann::json& params);

    /// Get workspace symbols
    /// @param params {"query": "search_term"}
    std::string WorkspaceSymbols(const nlohmann::json& params);

    /// Format document
    /// @param params {"uri": "file://..."}
    std::string FormatDocument(const nlohmann::json& params);

    /// Rename symbol
    /// @param params {"uri": "file://...", "line": 10, "character": 5, "new_name": "newName"}
    std::string RenameSymbol(const nlohmann::json& params);

    /// Get all diagnostics
    /// @param params {}
    std::string GetAllDiagnostics(const nlohmann::json& params);

    /// List registered LSP servers
    /// @param params {}
    std::string ListServers(const nlohmann::json& params);

private:
    LspTool() = default;

    /// Convert LSP Diagnostic to JSON
    nlohmann::json DiagnosticToJson(const Diagnostic& diag);

    /// Convert LSP Symbol to JSON
    nlohmann::json SymbolToJson(const Symbol& sym);

    /// Parse URI to file path
    std::string UriToPath(const std::string& uri);

    /// Parse file path to URI
    std::string PathToUri(const std::string& path);
};

}  // namespace aicode
