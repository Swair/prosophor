// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ai_coding.h"

#include <csignal>
#include <iostream>
#include <sstream>
#include "banner.h"
#include "prosophor_core/public/agent_engine.h"
#include "common/constants.h"
#include "common/log_wrapper.h"
#include "platform/platform.h"
#include "platform/input_handler.h"

namespace prosophor {

namespace {

AiCoding* g_tui_ptr = nullptr;

void SignalHandler(int /*sig*/) {
    if (g_tui_ptr) g_tui_ptr->Stop();
}

std::pair<std::string, std::vector<std::string>> ParseCommand(const std::string& line) {
    std::istringstream iss(line.substr(1));
    std::string cmd;
    iss >> cmd;
    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg) args.push_back(arg);
    return {cmd, args};
}
}  // namespace

AiCoding& AiCoding::GetInstance() {
    static AiCoding instance;
    return instance;
}

void AiCoding::RegisterCallbacks() {
    auto& engine = AgentEngine::GetInstance();

    engine.SetOutputCallback(
        [this](const std::string& session_id, const std::string& /*role_id*/,
               AgentRuntimeState state, const std::string& state_msg,
               const std::optional<MessageSchema>& reply, const std::string& delta) {
            // 主动触发会话在后台运行，不把它的流式帧混进用户终端
            if (session_id != session_id_) return;
            switch (state) {
                case AgentRuntimeState::BEGINNING:
                    break;
                case AgentRuntimeState::STREAM_THINKING_START:
                    std::cout << ColorCode::kCyan << "\n<thinking> " << ColorCode::kReset << std::flush;
                    break;
                case AgentRuntimeState::STREAM_THINKING:
                    // 流式帧增量在 delta 参数(思考增量)
                    std::cout << ColorCode::kGray << delta << ColorCode::kReset << std::flush;
                    break;
                case AgentRuntimeState::STREAM_THINKING_END:
                    std::cout << ColorCode::kCyan << " </thinking>\n" << ColorCode::kReset << std::flush;
                    break;
                case AgentRuntimeState::STREAM_CONTENT_START:
                    std::cout << ColorCode::kGreen << "● " << ColorCode::kReset << std::flush;
                    break;
                case AgentRuntimeState::STREAM_CONTENT_TYPING:
                    // 流式帧增量在 delta 参数(正文增量)
                    std::cout << delta << std::flush;
                    break;
                case AgentRuntimeState::STREAM_CONTENT_END:
                    std::cout << ColorCode::kGray << "\n●" << ColorCode::kReset << std::flush;
                    break;
                case AgentRuntimeState::STREAM_TOOL_START:
                    std::cout << ColorCode::kGreen << "." << ColorCode::kReset << std::flush;
                    break;
                case AgentRuntimeState::STREAM_TOOL:
                    std::cout << ColorCode::kGray << "." << ColorCode::kReset << std::flush;
                    break;
                case AgentRuntimeState::STREAM_TOOL_END:
                    std::cout << ColorCode::kRed << "." << ColorCode::kReset << std::flush;
                    break;
                case AgentRuntimeState::EXECUTING_TOOL:
                    std::cout << ColorCode::kCyan << "<executing tool> " << ColorCode::kReset << ColorCode::kGray << state_msg << ColorCode::kReset << std::endl;
                    break;
                case AgentRuntimeState::STREAM_MODE_COMPLETE:
                    std::cout << ColorCode::kGray << "\n●●> " << ColorCode::kReset << std::flush;
                    break;
                case AgentRuntimeState::COMPLETE:
                    if (reply) std::cout << ColorCode::kGreen << "● " << ColorCode::kReset << reply->text() << std::endl;
                    break;
                case AgentRuntimeState::STATE_ERROR:
                    std::cout << ColorCode::kRed << "● " << state_msg.c_str() << ColorCode::kReset << std::endl;
                    break;
                default:
                    break;
            }
        });

    engine.SetProactiveMessageCallback(
        [](const std::string& source, const std::string& role_id,
           const std::string& /*session_id*/, const std::string& text) {
            std::cout << "\n" << ColorCode::kCyan << "[主动] " << ColorCode::kReset
                      << "(" << source << " / " << role_id << ") " << text << std::endl;
        });

    engine.SetPermissionCallback(
        [](const std::string& tool_name, const nlohmann::json& input,
           const std::string& reason) -> bool {
            std::string command_desc;
            if (tool_name == "bash" && input.contains("command")) {
                command_desc = input["command"].get<std::string>();
                if (command_desc.length() > 60) {
                    command_desc = command_desc.substr(0, 57) + "...";
                }
            }
            std::cout << "\n[Permission Required] Tool: " << tool_name;
            if (!command_desc.empty()) std::cout << "  Command: " << command_desc;
            std::cout << "  Reason: " << reason;
            std::cout << "\n  Allow this action? [Y/N]: " << std::flush;
            std::string response = Platform::ReadConsoleLine();
            return response == "y" || response == "Y";
        });
}

void AiCoding::HandleInputEvent(const InputEvent& event) {
    if (event.IsInterrupt()) {
        interrupted_ = true;
        AgentEngine::GetInstance().StopSession(session_id_);
        return;
    }

    auto& engine = AgentEngine::GetInstance();

    if (event.IsCommand()) {
        const auto& cmd = event.GetCommand().command;
        if (cmd == "exit" || cmd == "quit" || cmd == "bye") {
            interrupted_ = true;
            return;
        }
        engine.SendUserMessage(session_id_, event.GetCommandText());
    } else if (event.IsText()) {
        if (Platform::kIsWindows) {
            std::cout << "> " << event.GetText() << std::endl;
        }
        engine.SendUserMessage(session_id_, event.GetText());
    }
}

int AiCoding::Run() {
    g_tui_ptr = this;
    std::signal(SIGINT, SignalHandler);

    PrintBanner(PROSOPHOR_VERSION);
    RegisterCallbacks();

    auto& cfg = AgentEngine::GetInstance().GetConfig();
    std::string tui_role = cfg.default_role.empty() ? "default" : cfg.default_role[0];
    session_id_ = AgentEngine::GetInstance().CreateSession(tui_role, "TUI session");

    InputHandler input_handler;

    LOG_INFO("AiCoding: starting input loop");
    std::cout << "> " << std::flush;
    while (!interrupted_) {
        std::string line = input_handler.ReadLine("");
        std::cout << "> " << std::flush;
        if (line.empty()) continue;

        if (line[0] == '/') {
            auto [cmd, args] = ParseCommand(line);
            HandleInputEvent(InputEvent{
                InputSource::Terminal,
                InputEvent::Type::Command,
                CommandInputEvent{cmd, args}
            });
        } else {
            HandleInputEvent(InputEvent{
                InputSource::Terminal,
                InputEvent::Type::Text,
                TextInputEvent{line}
            });
        }
    }
    LOG_INFO("AiCoding: input loop ended");

    input_handler.DisableRawMode();
    return 0;
}

void AiCoding::Stop() {
    interrupted_ = true;
}

}  // namespace prosophor
