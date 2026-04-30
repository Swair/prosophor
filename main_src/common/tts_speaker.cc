// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/tts_speaker.h"
#include "common/log_wrapper.h"
#include "common/file_utils.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <signal.h>
#endif

namespace prosophor {

namespace {

constexpr const char* TTS_OUTPUT_DIR = "assets/tts_cache/";
constexpr int TTS_TIMEOUT_MS = 30000;  // edge-tts 需要较长超时

/// 生成唯一的输出文件名
std::string MakeOutputPath() {
    static int seq = 0;
    seq++;
    return std::string(TTS_OUTPUT_DIR) + "tts_" + std::to_string(seq) + ".wav";
}

/// 转义命令行参数
std::string EscapeArg(const std::string& arg) {
#ifdef _WIN32
    return "\"" + arg + "\"";
#else
    std::string result = "\"";
    for (char c : arg) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') result += '\\';
        result += c;
    }
    result += "\"";
    return result;
#endif
}

}  // namespace

TtsSpeaker& TtsSpeaker::GetInstance() {
    static TtsSpeaker instance;
    return instance;
}

void TtsSpeaker::Initialize() {
    // 确保缓存目录存在
    EnsureDirectory(TTS_OUTPUT_DIR);
    LOG_INFO("TtsSpeaker initialized (voice: {}).", voice_);
}

void TtsSpeaker::SetOnSynthesized(OnSynthesizedCallback cb) {
    on_synthesized_ = cb;
}

void TtsSpeaker::Speak(const std::string& text) {
    if (text.empty() || speaking_) return;

    std::thread([this, text]() {
        SpeakAsync(text);
    }).detach();
}

void TtsSpeaker::SpeakAsync(const std::string& text) {
    speaking_ = true;

    std::string wav_path = Synthesize(text);

    speaking_ = false;

    if (!wav_path.empty() && on_synthesized_) {
        on_synthesized_(wav_path);
    }
}

bool TtsSpeaker::IsSpeaking() const {
    return speaking_;
}

std::string TtsSpeaker::Synthesize(const std::string& text) {
    std::string wav_path = MakeOutputPath();

    // 构建命令行
    std::string cmd = "edge-tts -t " + EscapeArg(text)
                    + " -v " + EscapeArg(voice_)
                    + " --write-media " + EscapeArg(wav_path);

#ifdef _WIN32
    std::string full_cmd = "cmd.exe /c " + cmd;
#else
    std::string full_cmd = "/bin/sh -c " + EscapeArg(cmd);
#endif

    LOG_DEBUG("TTS command: {}", full_cmd);

    // 执行子进程
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        LOG_ERROR("TtsSpeaker: CreatePipe failed");
        return "";
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::string cmd_line = full_cmd;
    if (!CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, TRUE, 0,
                         nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        LOG_ERROR("TtsSpeaker: CreateProcess failed, error={}", GetLastError());
        return "";
    }

    CloseHandle(hWrite);

    DWORD wait_result = WaitForSingleObject(pi.hProcess, TTS_TIMEOUT_MS);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, -1);
        LOG_WARN("TtsSpeaker: edge-tts timeout");
    } else {
        DWORD exit_code;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        if (exit_code != 0) {
            LOG_ERROR("TtsSpeaker: edge-tts exit code={}", exit_code);
        }
    }

    // 读取子进程输出
    char buffer[4096];
    DWORD bytes_read = 0;
    BOOL read_ok = ::ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytes_read, nullptr);
    if (read_ok && bytes_read > 0) {
        buffer[bytes_read] = '\0';
    } else {
        buffer[0] = '\0';
    }

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

#else
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOG_ERROR("TtsSpeaker: pipe failed");
        return "";
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        LOG_ERROR("TtsSpeaker: fork failed");
        return "";
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    // 等待子进程完成（带超时）
    auto start = std::chrono::steady_clock::now();
    bool done = false;
    while (true) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result != 0) { done = true; break; }

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= std::chrono::milliseconds(TTS_TIMEOUT_MS)) {
            kill(pid, SIGKILL);
            LOG_WARN("TtsSpeaker: edge-tts timeout");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 读取输出
    char buffer[4096];
    std::string output;
    ssize_t n;
    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, n);
    }
    close(pipefd[0]);
#endif

    // 检查 WAV 文件是否生成
    if (FileExists(wav_path)) {
        LOG_INFO("TtsSpeaker: synthesized {} ({} bytes)", wav_path,
                 static_cast<int>(std::filesystem::file_size(wav_path)));
        return wav_path;
    }

    LOG_ERROR("TtsSpeaker: output file not found: {}", wav_path);
    return "";
}

}  // namespace prosophor
