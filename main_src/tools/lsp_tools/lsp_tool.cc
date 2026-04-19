// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "lsp_tool.h"

#include <sstream>
#include <filesystem>
#include <algorithm>

#include "common/log_wrapper.h"

namespace aicode {

namespace fs = std::filesystem;

LspTool& LspTool::GetInstance() {
    static LspTool instance;
    return instance;
}

std::string LspTool::UriToPath(const std::string& uri) {
    // Convert file:// URI to path
    if (uri.size() >= 7 && uri.compare(0, 7, "file://") == 0) {
        return uri.substr(7);
    }
    // If it's already a path, return as-is
    return uri;
}

std::string LspTool::PathToUri(const std::string& path) {
    // Convert path to file:// URI
    fs::path p = fs::absolute(path);
    return "file://" + p.string();
}

nlohmann::json LspTool::DiagnosticToJson(const Diagnostic& diag) {
    nlohmann::json j;
    j["uri"] = diag.uri;
    j["line"] = diag.line;
    j["character"] = diag.character;

    switch (diag.severity) {
        case DiagnosticSeverity::Error:
            j["severity"] = "error";
            break;
        case DiagnosticSeverity::Warning:
            j["severity"] = "warning";
            break;
        case DiagnosticSeverity::Information:
            j["severity"] = "information";
            break;
        case DiagnosticSeverity::Hint:
            j["severity"] = "hint";
            break;
    }

    j["message"] = diag.message;
    j["source"] = diag.source;
    if (!diag.code.empty()) {
        j["code"] = diag.code;
    }
    return j;
}

nlohmann::json LspTool::SymbolToJson(const Symbol& sym) {
    nlohmann::json j;
    j["name"] = sym.name;
    j["kind"] = sym.kind;
    j["uri"] = sym.uri;
    j["line"] = sym.line;
    j["character"] = sym.character;
    return j;
}

std::string LspTool::Diagnostics(const nlohmann::json& params) {
    try {
        if (!params.contains("uri")) {
            return R"({"error": "Missing required parameter: uri"})";
        }

        std::string uri = params["uri"];
        std::string path = UriToPath(uri);

        // Start LSP server for this file if not running
        auto& lsp_mgr = LspManager::GetInstance();
        lsp_mgr.StartServerForFile(path);

        // Get diagnostics
        auto diagnostics = lsp_mgr.GetDiagnostics(uri);

        nlohmann::json result = nlohmann::json::object();
        result["uri"] = uri;
        result["diagnostics"] = nlohmann::json::array();

        // Filter by severity if specified
        std::string severity_filter;
        if (params.contains("severity")) {
            severity_filter = params["severity"];
        }

        for (const auto& diag : diagnostics) {
            if (severity_filter.empty()) {
                result["diagnostics"].push_back(DiagnosticToJson(diag));
            } else {
                std::string diag_severity;
                switch (diag.severity) {
                    case DiagnosticSeverity::Error: diag_severity = "error"; break;
                    case DiagnosticSeverity::Warning: diag_severity = "warning"; break;
                    case DiagnosticSeverity::Information: diag_severity = "information"; break;
                    case DiagnosticSeverity::Hint: diag_severity = "hint"; break;
                }
                if (diag_severity == severity_filter) {
                    result["diagnostics"].push_back(DiagnosticToJson(diag));
                }
            }
        }

        result["count"] = result["diagnostics"].size();

        // Add summary
        int error_count = 0, warning_count = 0;
        for (const auto& diag : diagnostics) {
            if (diag.severity == DiagnosticSeverity::Error) error_count++;
            else if (diag.severity == DiagnosticSeverity::Warning) warning_count++;
        }
        result["summary"] = {
            {"errors", error_count},
            {"warnings", warning_count}
        };

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

std::string LspTool::GoToDefinition(const nlohmann::json& params) {
    try {
        if (!params.contains("uri") || !params.contains("line") || !params.contains("character")) {
            return R"({"error": "Missing required parameters: uri, line, character"})";
        }

        std::string uri = params["uri"];
        int line = params["line"];
        int character = params["character"];

        std::string path = UriToPath(uri);
        auto& lsp_mgr = LspManager::GetInstance();

        // Start LSP server if not running
        lsp_mgr.StartServerForFile(path);

        auto symbols = lsp_mgr.GoToDefinition(uri, line, character);

        nlohmann::json result = nlohmann::json::object();
        result["uri"] = uri;
        result["position"] = {{"line", line}, {"character", character}};
        result["definitions"] = nlohmann::json::array();

        for (const auto& sym : symbols) {
            nlohmann::json def;
            def["uri"] = sym.uri;
            def["line"] = sym.line;
            def["character"] = sym.character;
            def["name"] = sym.name;
            result["definitions"].push_back(def);
        }

        if (symbols.empty()) {
            result["message"] = "No definition found";
        }

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

std::string LspTool::FindReferences(const nlohmann::json& params) {
    try {
        if (!params.contains("uri") || !params.contains("line") || !params.contains("character")) {
            return R"({"error": "Missing required parameters: uri, line, character"})";
        }

        std::string uri = params["uri"];
        int line = params["line"];
        int character = params["character"];
        bool include_declaration = params.value("include_declaration", true);

        std::string path = UriToPath(uri);
        auto& lsp_mgr = LspManager::GetInstance();

        // Start LSP server if not running
        lsp_mgr.StartServerForFile(path);

        auto symbols = lsp_mgr.FindReferences(uri, line, character);

        nlohmann::json result = nlohmann::json::object();
        result["uri"] = uri;
        result["position"] = {{"line", line}, {"character", character}};
        result["references"] = nlohmann::json::array();
        result["include_declaration"] = include_declaration;

        for (const auto& sym : symbols) {
            result["references"].push_back(SymbolToJson(sym));
        }

        result["count"] = symbols.size();

        if (symbols.empty()) {
            result["message"] = "No references found";
        }

        // Group by file
        std::unordered_map<std::string, int> file_counts;
        for (const auto& sym : symbols) {
            std::string file_path = UriToPath(sym.uri);
            fs::path p(file_path);
            file_counts[p.filename().string()]++;
        }

        result["by_file"] = nlohmann::json::object();
        for (const auto& [file, count] : file_counts) {
            result["by_file"][file] = count;
        }

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

std::string LspTool::GetHover(const nlohmann::json& params) {
    try {
        if (!params.contains("uri") || !params.contains("line") || !params.contains("character")) {
            return R"({"error": "Missing required parameters: uri, line, character"})";
        }

        std::string uri = params["uri"];
        int line = params["line"];
        int character = params["character"];

        std::string path = UriToPath(uri);
        auto& lsp_mgr = LspManager::GetInstance();

        // Start LSP server if not running
        lsp_mgr.StartServerForFile(path);

        std::string hover = lsp_mgr.GetHover(uri, line, character);

        nlohmann::json result = nlohmann::json::object();
        result["uri"] = uri;
        result["position"] = {{"line", line}, {"character", character}};
        result["contents"] = hover;

        if (hover.empty()) {
            result["message"] = "No hover information available";
        }

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

std::string LspTool::GetDocumentSymbols(const nlohmann::json& params) {
    try {
        if (!params.contains("uri")) {
            return R"({"error": "Missing required parameter: uri"})";
        }

        std::string uri = params["uri"];
        std::string path = UriToPath(uri);

        auto& lsp_mgr = LspManager::GetInstance();

        // Start LSP server if not running
        lsp_mgr.StartServerForFile(path);

        auto symbols = lsp_mgr.GetDocumentSymbols(uri);

        nlohmann::json result = nlohmann::json::object();
        result["uri"] = uri;
        result["symbols"] = nlohmann::json::array();

        for (const auto& sym : symbols) {
            result["symbols"].push_back(SymbolToJson(sym));
        }

        result["count"] = symbols.size();

        // Group by kind
        std::unordered_map<std::string, int> kind_counts;
        for (const auto& sym : symbols) {
            kind_counts[sym.kind]++;
        }
        result["by_kind"] = nlohmann::json::object();
        for (const auto& [kind, count] : kind_counts) {
            result["by_kind"][kind] = count;
        }

        if (symbols.empty()) {
            result["message"] = "No symbols found in document";
        }

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

std::string LspTool::WorkspaceSymbols(const nlohmann::json& params) {
    try {
        if (!params.contains("query")) {
            return R"({"error": "Missing required parameter: query"})";
        }

        std::string query = params["query"];
        auto& lsp_mgr = LspManager::GetInstance();

        auto symbols = lsp_mgr.WorkspaceSymbols(query);

        nlohmann::json result = nlohmann::json::object();
        result["query"] = query;
        result["symbols"] = nlohmann::json::array();

        for (const auto& sym : symbols) {
            result["symbols"].push_back(SymbolToJson(sym));
        }

        result["count"] = symbols.size();

        if (symbols.empty()) {
            result["message"] = "No symbols found matching query";
        }

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

std::string LspTool::FormatDocument(const nlohmann::json& params) {
    try {
        if (!params.contains("uri")) {
            return R"({"error": "Missing required parameter: uri"})";
        }

        std::string uri = params["uri"];
        std::string path = UriToPath(uri);

        auto& lsp_mgr = LspManager::GetInstance();

        // Start LSP server if not running
        lsp_mgr.StartServerForFile(path);

        std::string formatted = lsp_mgr.FormatDocument(uri);

        nlohmann::json result = nlohmann::json::object();
        result["uri"] = uri;
        result["formatted"] = formatted;

        if (formatted.empty()) {
            result["message"] = "Document formatting not available or no changes";
        }

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

std::string LspTool::RenameSymbol(const nlohmann::json& params) {
    try {
        if (!params.contains("uri") || !params.contains("line") ||
            !params.contains("character") || !params.contains("new_name")) {
            return R"({"error": "Missing required parameters: uri, line, character, new_name"})";
        }

        std::string uri = params["uri"];
        int line = params["line"];
        int character = params["character"];
        std::string new_name = params["new_name"];

        std::string path = UriToPath(uri);
        auto& lsp_mgr = LspManager::GetInstance();

        // Start LSP server if not running
        lsp_mgr.StartServerForFile(path);

        // Note: This requires LSP manager to implement rename functionality
        // For now, return a message indicating this feature needs implementation
        nlohmann::json result = nlohmann::json::object();
        result["uri"] = uri;
        result["position"] = {{"line", line}, {"character", character}};
        result["new_name"] = new_name;
        result["message"] = "Rename functionality requires LSP manager implementation";
        result["status"] = "not_implemented";

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

std::string LspTool::GetAllDiagnostics(const nlohmann::json& /*params*/) {
    try {
        auto& lsp_mgr = LspManager::GetInstance();
        auto diagnostics = lsp_mgr.GetAllDiagnostics();

        nlohmann::json result = nlohmann::json::object();
        result["diagnostics"] = nlohmann::json::array();

        int error_count = 0, warning_count = 0;

        for (const auto& diag : diagnostics) {
            result["diagnostics"].push_back(DiagnosticToJson(diag));
            if (diag.severity == DiagnosticSeverity::Error) error_count++;
            else if (diag.severity == DiagnosticSeverity::Warning) warning_count++;
        }

        result["count"] = diagnostics.size();
        result["summary"] = {
            {"errors", error_count},
            {"warnings", warning_count}
        };

        // Group by file
        std::unordered_map<std::string, int> file_counts;
        for (const auto& diag : diagnostics) {
            std::string file_path = UriToPath(diag.uri);
            fs::path p(file_path);
            file_counts[p.filename().string()]++;
        }

        result["by_file"] = nlohmann::json::object();
        for (const auto& [file, count] : file_counts) {
            result["by_file"][file] = count;
        }

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

std::string LspTool::ListServers(const nlohmann::json& /*params*/) {
    try {
        auto& lsp_mgr = LspManager::GetInstance();
        auto servers = lsp_mgr.GetRegisteredServers();

        nlohmann::json result = nlohmann::json::object();
        result["servers"] = nlohmann::json::array();

        for (const auto& server : servers) {
            result["servers"].push_back(server);
        }

        result["count"] = servers.size();

        return result.dump(2);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}}.dump(2);
    }
}

}  // namespace aicode
