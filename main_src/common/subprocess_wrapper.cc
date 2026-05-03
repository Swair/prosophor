// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#include "common/subprocess_wrapper.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#endif

#include "common/log_wrapper.h"

namespace prosophor {

#ifdef _WIN32

SubprocessResult ExecuteScriptWithTimeout(const std::string& script_path, int timeout_ms) {
    SubprocessResult result;

    // Windows 实现：使用 CreateProcess
    std::string cmd;

    // 根据脚本扩展名选择执行方式
    if (script_path.size() > 3 &&
        script_path.substr(script_path.size() - 3) == ".py") {
        // Python 脚本：使用 py 或 python 执行
        cmd = "cmd.exe /c \"py " + script_path + "\"";
    } else if (script_path.size() > 4 &&
               script_path.substr(script_path.size() - 4) == ".bat") {
        // 批处理文件
        cmd = "cmd.exe /c \"" + script_path + "\"";
    } else if (script_path.size() > 4 &&
               script_path.substr(script_path.size() - 4) == ".cmd") {
        // CMD 脚本
        cmd = "cmd.exe /c \"" + script_path + "\"";
    } else {
        // 其他脚本（带 shebang 的可执行脚本）
        cmd = "cmd.exe /c \"" + script_path + "\"";
    }

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        result.return_code = -1;
        result.error_output = "Failed to create pipe";
        return result;
    }

    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, (LPSTR)cmd.c_str(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        result.return_code = -1;
        result.error_output = "Failed to create process";
        return result;
    }

    CloseHandle(hWrite);

    // 等待进程完成或超时
    DWORD wait_result = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout_ms));
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, -1);
        result.timeout = true;
        result.return_code = -1;
        result.error_output = "Script timeout after " + std::to_string(timeout_ms) + "ms";
        LOG_WARN("Script timeout: {}", script_path);
    } else {
        DWORD exit_code;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        result.return_code = static_cast<int>(exit_code);
    }

    // 读取输出
    char buffer[4096];
    DWORD bytes_read;
    if (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytes_read, nullptr)) {
        buffer[bytes_read] = '\0';
        result.output = buffer;
    }

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
}

#else

SubprocessResult ExecuteScriptWithTimeout(const std::string& script_path, int timeout_ms) {
    SubprocessResult result;

    // Unix/Linux/macOS 实现：使用 fork + exec
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        result.return_code = -1;
        result.error_output = "Failed to create pipe";
        return result;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        result.return_code = -1;
        result.error_output = "Failed to fork";
        return result;
    }

    if (pid == 0) {
        // 子进程
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execl(script_path.c_str(), script_path.c_str(), nullptr);
        _exit(127);
    }

    // 父进程
    close(pipefd[1]);

    // 设置非阻塞
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    auto start = std::chrono::steady_clock::now();
    std::string output;

    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= std::chrono::milliseconds(timeout_ms)) {
            kill(pid, SIGKILL);
            result.timeout = true;
            result.return_code = -1;
            result.error_output = "Script timeout";
            LOG_WARN("Script timeout: {}", script_path);
            break;
        }

        char buffer[256];
        ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
        if (n > 0) {
            output.append(buffer, n);
        } else if (n == 0) {
            // EOF
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    close(pipefd[0]);
    waitpid(pid, nullptr, 0);

    result.output = output;
    return result;
}

#endif

}  // namespace prosophor
