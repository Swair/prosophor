// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// aidgen 工具调用协议（QNN 本地补全式 provider 专用）：
// schema → 提示词段、生成文本 → 调用块。
// 走 API tools 字段的 provider（openai/anthropic/llamacpp）不用这套 —— 它们有原生
// 工具通道，这里是 aidgen 没有原生工具 API 时的文本等价物。
// 全部纯函数、零状态：aidgen_provider.cc 依赖 QNN SDK 进不了测试构建，协议逻辑
// 单独成头，才可能被 tests/nutribot 单测覆盖。

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "providers/llm/llm_provider.h"   // ToolUseSchema
#include "tools/tool_registry.h"          // ToolsSchema

namespace prosophor {

// 标签包裹的 JSON，而不是裸 JSON：正文里出现 JSON 的场合太多（营养数据、
// 菜谱清单），裸 JSON 会被误判成调用。
// 标签名对齐 Qwen2.5 训练时的工具格式（<tool_call> / <tool_response>）——
// 板上实测用 <tool_result> 时模型认不出工具返回，拿到结果直接 EOS。
inline constexpr const char* tool_call_open = "<tool_call>";
inline constexpr const char* tool_call_close = "</tool_call>";
inline constexpr const char* tool_result_open = "<tool_response>";
inline constexpr const char* tool_result_close = "</tool_response>";

// 解析结果：调用列表 + 剥掉调用段后的剩余正文 + 坏调用计数（排查用）
struct ToolCallParseResult {
    std::vector<ToolUseSchema> tool_calls;
    std::string remaining_text;
    size_t malformed_count = 0;
};

// 按码点截断（不切半个汉字）。小上下文模型（cl2048）的提示词预算极紧：
// 工具长描述在提示词里截断，完整说明仍留在 schema 里给走原生 tools 通道的
// provider —— 本地补全式链路（0.5B）读到 1400 token 就开始空输出。
inline std::string TruncateForPrompt(const std::string& text, size_t max_codepoints) {
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (++count > max_codepoints) return text.substr(0, i) + "…";
    }
    return text;
}

// 剥两侧空白与换行（调用标签前后模型常多写换行）
inline std::string TrimWhitespaceEdges(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// 调用 JSON：键序 name 在前、arguments 在后（Qwen 训练格式的键序；nlohmann
// 按字母序会输出反序，和训练模式不一致 —— 端侧模型对格式细节敏感）
inline std::string DumpToolCallJson(const std::string& name,
                                    const nlohmann::json& arguments) {
    return "{\"name\": " + nlohmann::json(name).dump() +
           ", \"arguments\": " + arguments.dump() + "}";
}

// 调用块：按模型实际使用的裸调用风格回放（板上实测：Qwen2.5-VL 侧端输出
// robot_face(face="happy")，而不是 <tool_call> JSON）。历史与模型自己的输出
// 同构，否则它会看到一个"别人替它说的"格式，对不上话（拿到结果直接沉默）。
inline std::string RenderToolCallBlock(const std::string& name,
                                       const nlohmann::json& arguments) {
    std::string out = name + "(";
    bool first = true;
    if (arguments.is_object()) {
        for (const auto& [key, value] : arguments.items()) {
            if (!first) out += ", ";
            first = false;
            out += key + "=";
            out += value.is_string() ? nlohmann::json(value).dump() : value.dump();
        }
    }
    return out + ")";
}

// 结果块：纯文本回放 —— 端侧模型对 XML 工具标签不买账（实测拿到
// <tool_response> 后直接 EOS）。工具结果按普通 user 消息给出去，末尾紧跟
// 一句作答要求（贴着生成点，远处的 system 指令它不听）。
inline std::string RenderToolResultBlock(const std::string& content) {
    return "（工具返回：" + content + "）\n请用一句话简短回复用户。";
}

// 参数清单：JSON Schema → 紧凑单行。小模型读不动完整 schema 的嵌套，
// 名字/类型/枚举/必填/说明是它能消化并映射成参数值的全部信息量。
inline std::string RenderToolParams(const nlohmann::json& input_schema) {
    const auto props = input_schema.find("properties");
    if (props == input_schema.end() || !props->is_object() || props->empty()) {
        return "无参数";
    }
    std::vector<std::string> required;
    if (input_schema.contains("required") && input_schema["required"].is_array()) {
        for (const auto& name : input_schema["required"]) {
            if (name.is_string()) required.push_back(name.get<std::string>());
        }
    }
    std::string out;
    for (const auto& [name, spec] : props->items()) {
        if (!out.empty()) out += "；";
        out += name;
        if (spec.contains("type") && spec["type"].is_string()) {
            out += " " + spec["type"].get<std::string>();
        }
        if (spec.contains("enum") && spec["enum"].is_array()) {
            out += "[";
            for (size_t i = 0; i < spec["enum"].size(); ++i) {
                if (i > 0) out += ",";
                out += spec["enum"][i].is_string() ? spec["enum"][i].get<std::string>()
                                                   : spec["enum"][i].dump();
            }
            out += "]";
        }
        if (std::find(required.begin(), required.end(), name) != required.end()) {
            out += "(必填)";
        }
        if (spec.contains("description") && spec["description"].is_string()) {
            out += "：" + TruncateForPrompt(spec["description"].get<std::string>(), 28);
        }
    }
    return out;
}

// 归一化工具名：小模型常把 snake_case 的连字符吃掉（robot_face → robotface）。
// 比较时忽略大小写与下划线，命中后回填白名单里的原名（执行的必须是原名）。
inline std::string NormalizeToolName(const std::string& name) {
    std::string out;
    for (const char c : name) {
        if (c == '_') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

inline const ToolsSchema* FindWhitelistedTool(const std::vector<ToolsSchema>& tools,
                                              const std::string& name) {
    const std::string normalized = NormalizeToolName(name);
    for (const auto& tool : tools) {
        if (tool.name == name) return &tool;
        if (NormalizeToolName(tool.name) == normalized) return &tool;
    }
    return nullptr;
}

// 样例工具：挑参数最少的非零参数工具。格式示范要信息量最小；且小模型常原样
// 照抄样例 —— 板上实测它把样例里的移动指令抄去执行，底盘真跑了，所以样例工具
// 越无害越好（表情/查询优于移动）。
inline const ToolsSchema& PickExampleTool(const std::vector<ToolsSchema>& tools) {
    const ToolsSchema* picked = nullptr;
    for (const auto& tool : tools) {
        const auto props = tool.input_schema.find("properties");
        if (props == tool.input_schema.end() || !props->is_object() || props->empty()) {
            continue;
        }
        if (picked == nullptr || props->size() <
                                     picked->input_schema.at("properties").size()) {
            picked = &tool;
        }
    }
    return picked != nullptr ? *picked : tools.front();
}

inline nlohmann::json BuildExampleArguments(const ToolsSchema& tool) {
    nlohmann::json arguments = nlohmann::json::object();
    const auto props = tool.input_schema.find("properties");
    if (props != tool.input_schema.end() && props->is_object() && !props->empty()) {
        const auto first = props->items().begin();
        const std::string param_name = first.key();
        const nlohmann::json& spec = first.value();
        if (spec.contains("enum") && spec["enum"].is_array() && !spec["enum"].empty()) {
            arguments[param_name] = spec["enum"][0];
        } else if (spec.contains("type") && spec["type"] == "integer") {
            arguments[param_name] = 0;
        } else {
            arguments[param_name] = "值";
        }
    }
    return arguments;
}

// 样例调用：取样例工具的首个参数拼一行。具体样例对小模型远比抽象模板有效，
// 而样例不能硬编码某个角色专属的工具名 —— 从实际 tools 里生成。
inline std::string BuildToolCallExample(const std::vector<ToolsSchema>& tools) {
    const auto& tool = PickExampleTool(tools);
    return RenderToolCallBlock(tool.name, BuildExampleArguments(tool));
}

// AgentChat 样例：工具调用 + 同轮文字。必须用 <tool_call> 标签，裸调用后面
// 跟文字无法被 ParseBareToolCall 识别。
inline std::string BuildTaggedToolCallExample(const std::vector<ToolsSchema>& tools) {
    const auto& tool = PickExampleTool(tools);
    nlohmann::json call = {{"name", tool.name},
                           {"arguments", BuildExampleArguments(tool)}};
    return "<tool_call>" + call.dump() + "</tool_call>\n好的。";
}

// 单个工具的 Qwen 函数声明：{type:function, function:{name, description,
// parameters}}。参数 schema 只留 type/enum/截断说明 —— 提示词预算紧。
inline nlohmann::json CompactToolSchema(const ToolsSchema& tool) {
    nlohmann::json properties = nlohmann::json::object();
    const auto props = tool.input_schema.find("properties");
    if (props != tool.input_schema.end() && props->is_object()) {
        for (const auto& [name, spec] : props->items()) {
            nlohmann::json compact = nlohmann::json::object();
            if (spec.contains("type")) compact["type"] = spec["type"];
            if (spec.contains("enum")) compact["enum"] = spec["enum"];
            if (spec.contains("description") && spec["description"].is_string()) {
                compact["description"] =
                    TruncateForPrompt(spec["description"].get<std::string>(), 28);
            }
            properties[name] = compact;
        }
    }
    nlohmann::json parameters = {{"type", "object"}, {"properties", properties}};
    if (tool.input_schema.contains("required")) {
        parameters["required"] = tool.input_schema["required"];
    }
    return {{"type", "function"},
            {"function",
             {{"name", tool.name},
              {"description", TruncateForPrompt(tool.description, 36)},
              {"parameters", parameters}}}};
}

// 工具段：拼进 system 末尾（无工具返回空串，提示词零变化）。
// 结构与措辞对齐 Qwen2.5 训练时的函数调用模板（# Tools / <tools> JSON /
// <tool_call> 输出格式）—— 板上实测：自定义人读格式下，模型拿到 tool_response
// 后直接 EOS；对齐训练格式它才继续自然作答。
inline std::string BuildToolsPrompt(const std::vector<ToolsSchema>& tools,
                                    bool agent_chat = false) {
    if (tools.empty()) return "";
    std::string out =
        "\n\n# Tools\n\n"
        "You may call one or more functions to assist with the user query.\n\n"
        "You are provided with function signatures within <tools></tools> XML tags:\n"
        "<tools>\n";
    for (const auto& tool : tools) {
        out += CompactToolSchema(tool).dump() + "\n";
    }
    out += "</tools>\n\n";
    if (agent_chat) {
        // AgentChat 开环：工具调用和文字可以同时返回，文字会被播报。
        out += "需要执行动作时，先输出 <tool_call>…</tool_call>，再另起一行输出"
               "要对用户说的话；即使只执行动作，也必须给出这句文字。"
               "不需要动作时直接输出文字。\n"
               "例：\n" + BuildTaggedToolCallExample(tools) + "\n";
    } else {
        out += "需要执行动作时，只输出一行函数调用，不要加解释：\n";
        out += "例：" + BuildToolCallExample(tools) + "\n";
        // 结果轮行为显式教：模型收到工具结果后应继续作答而不是再调工具
        out += "收到工具返回的结果后，用一句话简短回复用户，不要再调用工具。\n";
    }
    return out;
}

// 类型归一：模型常把数值写成字符串（"servo_id":"6"）。按 schema 的
// integer/number 声明把可解析的字符串转回数字 —— 工具边界只管业务校验。
inline void CoerceArgumentsToSchema(const nlohmann::json& input_schema,
                                    nlohmann::json& arguments) {
    if (!arguments.is_object()) return;
    const auto props = input_schema.find("properties");
    if (props == input_schema.end() || !props->is_object()) return;
    for (const auto& [name, spec] : props->items()) {
        if (!arguments.contains(name)) continue;
        const std::string type = spec.value("type", "");
        nlohmann::json& value = arguments[name];
        if ((type != "integer" && type != "number") || !value.is_string()) continue;
        const std::string text = TrimWhitespaceEdges(value.get<std::string>());
        try {
            size_t consumed = 0;
            if (type == "integer") {
                const long long parsed = std::stoll(text, &consumed);
                if (consumed == text.size()) value = parsed;
            } else {
                const double parsed = std::stod(text, &consumed);
                if (consumed == text.size()) value = parsed;
            }
        } catch (const std::exception&) {
            // 转不动就保持原值，工具边界会给出明确报错
        }
    }
}

// 补全截断的 JSON：小模型常在参数中途被 EOS 截断。按括号栈补右括号，
// 字符串内的括号不计数（值里带 { 的文案不该被当成结构）。
inline std::string CloseUnbalancedBrackets(const std::string& text) {
    std::vector<char> stack;
    bool in_string = false;
    char prev = '\0';
    for (const char c : text) {
        if (in_string) {
            if (c == '"' && prev != '\\') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            stack.push_back('}');
        } else if (c == '[') {
            stack.push_back(']');
        } else if ((c == '}' || c == ']') && !stack.empty() && stack.back() == c) {
            stack.pop_back();
        }
        prev = c;
    }
    std::string out = text;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) out += *it;
    return out;
}

// 缺 name 时按参数键推断：参数键必须全部是某工具 schema 的属性，且只有一个
// 工具匹配（多个匹配宁可放弃 —— 执行错误动作比不执行更糟）。
inline const ToolsSchema* InferToolByArguments(const std::vector<ToolsSchema>& tools,
                                               const nlohmann::json& parsed) {
    const auto args = parsed.find("arguments");
    if (args == parsed.end() || !args->is_object() || args->empty()) return nullptr;
    const ToolsSchema* match = nullptr;
    for (const auto& tool : tools) {
        const auto props = tool.input_schema.find("properties");
        if (props == tool.input_schema.end() || !props->is_object()) continue;
        bool all_known = true;
        for (const auto& [key, value] : args->items()) {
            if (!props->contains(key)) {
                all_known = false;
                break;
            }
        }
        if (!all_known) continue;
        if (match != nullptr) return nullptr;
        match = &tool;
    }
    return match;
}

// 参数名归一：小模型会吃掉下划线/改大小写（has_food → hasfood）。
// 返回 schema 里的规范名；未命中返回空串。
inline std::string FindSchemaParamName(const nlohmann::json& props,
                                       const std::string& raw_key) {
    const std::string key = TrimWhitespaceEdges(raw_key);
    if (props.contains(key)) return key;
    const std::string normalized = NormalizeToolName(key);
    for (const auto& [name, spec] : props.items()) {
        (void)spec;
        if (NormalizeToolName(name) == normalized) return name;
    }
    return {};
}

// 单个调用段 → ToolUseSchema。只认白名单里的名字 —— 正文里的任意 JSON 不得触发
// 执行；arguments 必须是对象。命中后回填白名单原名。
inline bool ParseToolCallBody(const std::vector<ToolsSchema>& tools,
                              const std::string& body, ToolUseSchema& out) {
    const std::string trimmed = TrimWhitespaceEdges(body);
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(trimmed);
    } catch (const nlohmann::json::exception&) {
        try {
            parsed = nlohmann::json::parse(CloseUnbalancedBrackets(trimmed));
        } catch (const nlohmann::json::exception&) {
            return false;
        }
    }
    if (!parsed.is_object()) return false;
    const ToolsSchema* tool = nullptr;
    nlohmann::json arguments = nlohmann::json::object();
    if (parsed.contains("name") && parsed["name"].is_string()) {
        tool = FindWhitelistedTool(tools, parsed["name"].get<std::string>());
        arguments = parsed.value("arguments", nlohmann::json::object());
    } else {
        // 无 name 的裸 JSON：整个对象就是参数，按参数键推断工具
        tool = InferToolByArguments(tools, parsed);
        arguments = parsed;
    }
    if (tool == nullptr || !arguments.is_object()) return false;

    // 参数名归一（hasfood → has_food）；未知字段忽略
    const auto props = tool->input_schema.find("properties");
    nlohmann::json normalized = nlohmann::json::object();
    if (props != tool->input_schema.end() && props->is_object()) {
        for (const auto& [key, value] : arguments.items()) {
            const std::string canonical = FindSchemaParamName(*props, key);
            if (!canonical.empty()) normalized[canonical] = value;
        }
    } else {
        normalized = std::move(arguments);
    }
    out.name = tool->name;
    out.arguments = std::move(normalized);
    CoerceArgumentsToSchema(tool->input_schema, out.arguments);
    return true;
}

// 值字面量：引号串 / JSON 数组/对象 / 整数 / 小数 / true·false / 裸词。
// 先按 JSON 解析（覆盖 dishes=["红烧肉", ...]），失败再当裸词（robot_face(happy)）。
inline nlohmann::json ParseArgValue(const std::string& raw) {
    const std::string text = TrimWhitespaceEdges(raw);
    if (text.size() >= 2 && text.front() == '\'' && text.back() == '\'') {
        return text.substr(1, text.size() - 2);
    }
    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception&) {
        return text;
    }
}

// 按顶层逗号切参数：数组/对象/引号里的逗号不是分隔符
// （dishes=["红烧肉", "清蒸鲈鱼"] 是一个参数）。
inline std::vector<std::string> SplitBareArguments(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    bool in_string = false;
    char quote = '\0';
    bool escaped = false;
    for (const char c : text) {
        if (in_string) {
            current += c;
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                in_string = false;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = true;
            quote = c;
            current += c;
        } else if (c == '[' || c == '{' || c == '(') {
            ++depth;
            current += c;
        } else if (c == ']' || c == '}' || c == ')') {
            if (depth > 0) --depth;
            current += c;
        } else if (c == ',' && depth == 0) {
            parts.push_back(TrimWhitespaceEdges(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!TrimWhitespaceEdges(current).empty()) {
        parts.push_back(TrimWhitespaceEdges(current));
    }
    return parts;
}

// Python 风格裸调用兜底：name(值) / name(参数=值, ...)。整段输出必须就是调用本身
// 才接受 —— 正文里提到工具名不得触发执行。位置参数只在工具恰有单个参数时接受：
// 多参数的位置语义（robot_servo(70)）无法从文本恢复，宁可不执行。
inline bool ParseBareToolCall(const std::string& text,
                              const std::vector<ToolsSchema>& tools,
                              ToolUseSchema& out) {
    const std::string trimmed = TrimWhitespaceEdges(text);
    const size_t paren = trimmed.find('(');
    if (paren == std::string::npos || trimmed.back() != ')') return false;
    const ToolsSchema* tool = FindWhitelistedTool(tools, trimmed.substr(0, paren));
    if (tool == nullptr) return false;

    const std::string args_text =
        trimmed.substr(paren + 1, trimmed.size() - paren - 2);
    const auto props = tool->input_schema.find("properties");
    const bool has_props =
        props != tool->input_schema.end() && props->is_object() && !props->empty();

    nlohmann::json arguments = nlohmann::json::object();
    std::vector<std::string> positional;
    for (const std::string& part : SplitBareArguments(args_text)) {
        if (part.empty()) continue;
        const size_t equal = part.find('=');
        if (equal == std::string::npos) {
            positional.push_back(part);
            continue;
        }
        if (!has_props) return false;
        // 未知参数直接忽略：模型常多带 time/说明字段，不能因此丢掉整次调用
        const std::string key =
            FindSchemaParamName(*props, part.substr(0, equal));
        if (key.empty()) continue;
        arguments[key] = ParseArgValue(part.substr(equal + 1));
    }
    if (!positional.empty()) {
        // 单个参数的工具才接受位置参数；位置参数与关键词参数不得混用
        if (positional.size() > 1 || !arguments.empty() || !has_props ||
            props->size() != 1) {
            return false;
        }
        arguments[props->items().begin().key()] = ParseArgValue(positional.front());
    }
    CoerceArgumentsToSchema(tool->input_schema, arguments);
    out.name = tool->name;
    out.arguments = std::move(arguments);
    return true;
}

// 生成文本 → 工具调用。剥出 <tool_call>…</tool_call> 段解析；标签包着但解析
// 不了的段直接丢弃（计入 malformed_count）—— 那是模型的坏调用，念出来只会是
// 乱码（板上实测：残句被 TTS 播报）。未闭合标签（生成被截断）按到文本尾收尾。
// 整段文本没有任何标签调用时，再看它是不是一条裸的 Python 式调用。
inline ToolCallParseResult ParseToolCalls(const std::string& text,
                                          const std::vector<ToolsSchema>& tools) {
    ToolCallParseResult result;
    if (tools.empty()) {
        result.remaining_text = text;
        return result;
    }
    size_t cursor = 0;
    while (cursor < text.size()) {
        const size_t open = text.find(tool_call_open, cursor);
        if (open == std::string::npos) {
            result.remaining_text += text.substr(cursor);
            break;
        }
        result.remaining_text += text.substr(cursor, open - cursor);
        const size_t body_begin = open + std::strlen(tool_call_open);
        const size_t close = text.find(tool_call_close, body_begin);
        const size_t body_end = (close == std::string::npos) ? text.size() : close;
        const size_t segment_end = (close == std::string::npos)
                                       ? text.size()
                                       : close + std::strlen(tool_call_close);

        ToolUseSchema call;
        call.id = "call_" + std::to_string(result.tool_calls.size() + 1);
        if (ParseToolCallBody(tools, text.substr(body_begin, body_end - body_begin), call)) {
            result.tool_calls.push_back(std::move(call));
        } else {
            ++result.malformed_count;   // 坏调用：不进正文，别让 TTS 念乱码
        }
        cursor = segment_end;
    }
    if (result.tool_calls.empty()) {
        ToolUseSchema bare;
        bare.id = "call_1";
        // 裸 Python 调用，或整段就是一条 JSON 调用（模型有时忽略 <tool_call> 标签）
        if (ParseBareToolCall(text, tools, bare) ||
            ParseToolCallBody(tools, text, bare)) {
            result.tool_calls.push_back(std::move(bare));
            result.remaining_text.clear();
            return result;
        }

        // 兼容 "第一行裸调用 + 后续文字"（AgentChat 常见格式）：
        //   robot_face(face="happy")
        //   好呀，给你笑一个。
        const std::string trimmed_text = TrimWhitespaceEdges(text);
        const size_t newline = trimmed_text.find('\n');
        if (newline != std::string::npos) {
            ToolUseSchema line_call;
            line_call.id = "call_1";
            if (ParseBareToolCall(trimmed_text.substr(0, newline), tools,
                                  line_call)) {
                result.tool_calls.push_back(std::move(line_call));
                result.remaining_text =
                    TrimWhitespaceEdges(trimmed_text.substr(newline + 1));
                return result;
            }
        }
    }
    result.remaining_text = TrimWhitespaceEdges(result.remaining_text);
    return result;
}

}  // namespace prosophor
