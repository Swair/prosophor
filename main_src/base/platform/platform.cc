// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "platform/platform.h"
#include "common/file_utils.h"
#include "common/string_utils.h"

#include <chrono>
#include <cctype>
#include <csignal>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>  // getaddrinfo / inet_ntop / INET_ADDRSTRLEN
#include <windows.h>
#include <commdlg.h>
#include <iphlpapi.h>  // GetAdaptersAddresses(局域网 IP 枚举)
#include <shlobj.h>
#include <shellapi.h>
#include <process.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <sys/file.h>  // flock()
#endif

namespace prosophor {

std::string Platform::NativeToUtf8(const std::string& input) {
    if (IsUtf8(input)) {
        return input;
    }

#ifdef _WIN32
    int wide_len = MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, nullptr, 0);
    if (wide_len > 0) {
        std::wstring wide(wide_len, 0);
        MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, &wide[0], wide_len);

        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8_len > 0) {
            std::string utf8(utf8_len, 0);
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8_len, nullptr, nullptr);
            return utf8;
        }
    }
#endif

    return input;
}

std::string Platform::ReadLine() {
#ifdef _WIN32
    HANDLE h_input = GetStdHandle(STD_INPUT_HANDLE);
    std::string line;

    if (h_input != INVALID_HANDLE_VALUE) {
        wchar_t wbuf[4096];
        DWORD chars_read = 0;
        if (ReadConsoleW(h_input, wbuf, sizeof(wbuf) / sizeof(wchar_t) - 1, &chars_read, nullptr)) {
            while (chars_read > 0 && (wbuf[chars_read - 1] == L'\r' || wbuf[chars_read - 1] == L'\n')) {
                chars_read--;
            }
            if (chars_read > 0) {
                std::wstring wline(wbuf, chars_read);
                int size = WideCharToMultiByte(CP_UTF8, 0, wline.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (size > 0) {
                    line.resize(size - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wline.c_str(), -1, &line[0], size, nullptr, nullptr);
                }
            }
        } else {
            std::getline(std::cin, line);
            line = NativeToUtf8(line);
        }
    } else {
        std::getline(std::cin, line);
        line = NativeToUtf8(line);
    }
    return line;
#else
    std::string line;
    std::getline(std::cin, line);
    return line;
#endif
}

std::wstring Platform::Utf8ToWide(const std::string& utf8_str) {
#ifdef _WIN32
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);
    if (wide_len <= 1) return L"";
    std::wstring wide(wide_len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, &wide[0], wide_len);
    return wide;
#else
    (void)utf8_str;
    return L"";
#endif
}

void Platform::SetConsoleUtf8() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Tell the C/C++ runtime to use UTF-8, so that std::cout and friends
    // pass UTF-8 bytes through without GBK conversion (critical in msys2/mingw).
    // "en_US.UTF-8" works on msys2; ".UTF8" works on MSVC/CoreApp.
    setlocale(LC_ALL, "en_US.UTF-8");

    // Enable ANSI/VT escape sequence processing (Windows 10+)
    HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h_out != INVALID_HANDLE_VALUE) {
        DWORD out_mode = 0;
        if (GetConsoleMode(h_out, &out_mode)) {
            out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(h_out, out_mode);
        }
    }
#endif
}

int Platform::GetPid() {
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}

std::string Platform::ShellEscape(const std::string& arg) {
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

std::string Platform::ReadConsoleLine() {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
    char buf[256] = {};
    DWORD charsRead = 0;
    if (ReadConsoleA(hConsole, buf, sizeof(buf) - 1, &charsRead, nullptr)) {
        while (charsRead > 0 && (buf[charsRead - 1] == '\r' || buf[charsRead - 1] == '\n')) {
            charsRead--;
        }
        return std::string(buf, charsRead);
    }
    return {};
#else
    std::string line;
    std::getline(std::cin, line);
    return line;
#endif
}

std::string Platform::GetSelfExePath() {
    std::string self_path;
#ifdef __linux__
    char self_buf[4096];
    ssize_t len = readlink("/proc/self/exe", self_buf, sizeof(self_buf) - 1);
    if (len > 0) {
        self_buf[len] = '\0';
        self_path = self_buf;
    }
#elif defined(__APPLE__)
    char self_buf[4096];
    uint32_t size = sizeof(self_buf);
    if (_NSGetExecutablePath(self_buf, &size) == 0) {
        self_path = self_buf;
    }
#elif defined(_WIN32)
    char self_buf[4096];
    GetModuleFileNameA(nullptr, self_buf, sizeof(self_buf));
    self_path = self_buf;
#endif
    return self_path;
}

std::string Platform::NormalizePath(const std::string& path) {
#ifdef _WIN32
    // Step 1: Convert POSIX-style /x/... to Windows-native X:\... format.
    // MinGW may supply paths like /e/ai_ws/... which GetFullPathNameA
    // treats as relative (not starting with X:\) and prepends CWD.
    std::string converted = path;
    if (converted.size() >= 3 && converted[0] == '/'
        && std::isalpha(static_cast<unsigned char>(converted[1]))
        && converted[2] == '/') {
        converted[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(converted[1])));
        converted[1] = ':';
    }

    // Step 2: Use Windows API to resolve relative paths (containing .. etc.)
    // to absolute. GetFullPathNameA uses the actual Windows CWD, not MSYS2's.
    char full[MAX_PATH];
    DWORD len = GetFullPathNameA(converted.c_str(), MAX_PATH, full, nullptr);
    if (len > 0 && len < MAX_PATH) {
        std::string result(full, len);
        for (auto& c : result) {
            if (c == '/') c = '\\';
        }
        return result;
    }
    return converted;
#else
    return path;
#endif
}

bool Platform::PathExists(const std::string& path) {
    return std::filesystem::exists(NormalizePath(path));
}

std::string Platform::SelectPlatformPath(const std::string& default_path, const std::string& win_path) {
#ifdef _WIN32
    return win_path.empty() ? default_path : win_path;
#else
    (void)win_path;
    return default_path;
#endif
}

bool Platform::CheckPortOpen(int port) {
    std::string url = "http://127.0.0.1:" + std::to_string(port);
    std::string result = Platform::RunShellCommand(
        ("curl -s -o " + std::string(NullDevice()) + " -w \"%{http_code}\" --connect-timeout 2 "
         + url + " 2>" + NullDevice()).c_str());
    return !result.empty();
}

std::string Platform::RunShellCommand(const char* cmd) {
#ifdef _WIN32
    FILE* fp = _popen(cmd, "r");
#else
    FILE* fp = popen(cmd, "r");
#endif
    if (!fp) return {};

    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        result += buf;
    }
#ifdef _WIN32
    _pclose(fp);
#else
    pclose(fp);
#endif
    return result;
}

const char* Platform::NullDevice() {
#ifdef _WIN32
    return "NUL";
#else
    return "/dev/null";
#endif
}

Subprocess Platform::LaunchProcess(const std::vector<std::string>& args) {
    if (args.empty()) return {};

#ifdef _WIN32
    std::string cmd_line;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmd_line += " ";
        if (args[i].find(' ') != std::string::npos) {
            cmd_line += "\"" + args[i] + "\"";
        } else {
            cmd_line += args[i];
        }
    }

    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    // Prepend MinGW bin directory to PATH so child processes (llama-server)
    // can find MinGW runtime DLLs at runtime.
    char saved_path[32768] = {};
    GetEnvironmentVariableA("PATH", saved_path, sizeof(saved_path));
    bool found_mingw = false;

    char module_path[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, module_path, sizeof(module_path))) {
        auto p = std::filesystem::path(module_path).parent_path();
        while (p.has_parent_path()) {
            auto candidate = p / "mingw64" / "bin";
            if (std::filesystem::exists(candidate)) {
                SetEnvironmentVariableA("PATH", (candidate.string() + ";" + saved_path).c_str());
                found_mingw = true;
                break;
            }
            p = p.parent_path();
        }
    }
    if (!found_mingw) {
        char ml_buf[MAX_PATH] = {};
        DWORD ml = GetEnvironmentVariableA("MINGW_PREFIX", ml_buf, sizeof(ml_buf));
        if (ml > 0 && ml < MAX_PATH) {
            SetEnvironmentVariableA("PATH", (std::string(ml_buf) + "/bin;" + saved_path).c_str());
        }
    }

    if (!CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
                        nullptr,
                        nullptr, &si, &pi)) {
        if (saved_path[0]) SetEnvironmentVariableA("PATH", saved_path);
        return {};
    }
    Subprocess proc{ static_cast<int>(pi.dwProcessId) };
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (saved_path[0]) SetEnvironmentVariableA("PATH", saved_path);
    return proc;
#else
    pid_t pid = fork();
    if (pid < 0) return {};

    if (pid == 0) {
        std::vector<const char*> argv;
        for (const auto& a : args) {
            argv.push_back(a.c_str());
        }
        argv.push_back(nullptr);

        signal(SIGHUP, SIG_IGN);

        int dev_null = open("/dev/null", O_WRONLY);
        if (dev_null >= 0) {
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
            close(dev_null);
        }

        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    return Subprocess{ static_cast<int>(pid) };
#endif
}

int Platform::LaunchDetachedCommand(const std::string& command) {
    // Run command and capture PID (script must background and echo PID via "&; echo $!")
    std::string pid_str = Platform::RunShellCommand(command.c_str());
    if (pid_str.empty()) return -1;
    return std::atoi(pid_str.c_str());
}

bool Platform::IsProcessAlive(int pid) {
    if (pid <= 0) return false;

#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) return false;
    DWORD exit_code;
    BOOL alive = GetExitCodeProcess(hProcess, &exit_code) && exit_code == STILL_ACTIVE;
    CloseHandle(hProcess);
    return alive != FALSE;
#else
    return kill(pid, 0) == 0;
#endif
}

bool Platform::KillProcess(int pid, bool force) {
    if (pid <= 0) return true;

#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProcess) return false;
    bool ok = TerminateProcess(hProcess, force ? 1 : 0) != 0;
    if (ok) WaitForSingleObject(hProcess, force ? 5000 : 3000);
    CloseHandle(hProcess);
    return ok;
#else
    if (force) {
        kill(pid, SIGKILL);
    } else {
        kill(pid, SIGTERM);
        int status;
        waitpid(pid, &status, WNOHANG);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
        }
    }
    int status;
    waitpid(pid, &status, 0);
    return true;
#endif
}

bool Platform::KillProcessByName(const std::string& name) {
#ifdef _WIN32
    std::string image_name = name;
    if (image_name.size() < 4 ||
        image_name.substr(image_name.size() - 4) != ".exe") {
        image_name += ".exe";
    }
    Platform::RunShellCommand(
        ("taskkill -IM " + image_name + " -F 2>nul").c_str());
    return true;
#else
    std::string cmd = "pkill -9 " + Platform::ShellEscape(name) + " 2>/dev/null";
    Platform::RunCommandWithOutput(cmd, 5);
    return true;
#endif
}

CommandOutput Platform::RunCommandWithOutput(const std::string& command,
                                    int timeout_seconds,
                                    const std::string& workdir) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr, write_pipe = nullptr;
    if (!::CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return {"Failed to create pipe", -1};
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE input_read = nullptr, input_write = nullptr;
    if (!::CreatePipe(&input_read, &input_write, &sa, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return {"Failed to create input pipe", -1};
    }
    SetHandleInformation(input_read, HANDLE_FLAG_INHERIT, 0);
    CloseHandle(input_write);

    std::wstring wcmd_line = Platform::Utf8ToWide(command);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = input_read;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring wworkdir = workdir.empty() ? L"" : Platform::Utf8ToWide(workdir);

    BOOL ok = CreateProcessW(nullptr, wcmd_line.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW,
                             nullptr,
                             wworkdir.empty() ? nullptr : wworkdir.c_str(),
                             &si, &pi);
    CloseHandle(write_pipe);

    if (!ok) {
        CloseHandle(input_read);
        CloseHandle(read_pipe);
        return {"Failed to create process", -1};
    }

    std::string result;
    char buffer[1024];
    auto start = std::chrono::steady_clock::now();

    for (;;) {
        DWORD wait_ms = 100;
        if (timeout_seconds > 0) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            int64_t timeout_ms = timeout_seconds * 1000;
            if (elapsed_ms >= timeout_ms) {
                TerminateProcess(pi.hProcess, 1);
                CloseHandle(input_read);
                CloseHandle(read_pipe);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return {"Command timeout", -2};
            }
            wait_ms = std::min(wait_ms, static_cast<DWORD>(timeout_ms - elapsed_ms));
        }

        DWORD avail = 0;
        if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail, nullptr)) {
            if (WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_OBJECT_0) break;
            continue;
        }
        if (avail == 0) {
            if (WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_OBJECT_0) break;
            continue;
        }

        DWORD bytes_read = 0;
        if (::ReadFile(read_pipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) && bytes_read > 0) {
            buffer[bytes_read] = '\0';
            result += NativeToUtf8(std::string(buffer, bytes_read));
        }
    }

    DWORD bytes_read = 0;
    while (::ReadFile(read_pipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        result += NativeToUtf8(std::string(buffer, bytes_read));
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(input_read);
    CloseHandle(read_pipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return {result, static_cast<int>(exit_code)};

#else
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return {"Failed to create pipe", -1};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {"Failed to fork", -1};
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (!workdir.empty()) {
            if (chdir(workdir.c_str()) != 0) {
                _exit(127);
            }
        }

        execl("/bin/sh", "/bin/sh", "-c", command.c_str(), (char*)nullptr);
        _exit(127);
    }

    close(pipefd[1]);

    std::string result;
    char buffer[4096];
    auto start = std::chrono::steady_clock::now();

    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100000;

    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(pipefd[0], &readfds);

        int select_result = select(pipefd[0] + 1, &readfds, nullptr, nullptr, &tv);

        if (select_result > 0 && FD_ISSET(pipefd[0], &readfds)) {
            ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                result += std::string(buffer, n);
            } else if (n == 0) {
                break;
            }
        }

        if (timeout_seconds > 0) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeout_seconds) {
                kill(pid, SIGKILL);
                close(pipefd[0]);
                waitpid(pid, nullptr, 0);
                return {"Command timeout", -2};
            }
        }

        int stat;
        int wait_result = waitpid(pid, &stat, WNOHANG);
        if (wait_result > 0) {
            while (read(pipefd[0], buffer, sizeof(buffer) - 1) > 0) {}
            close(pipefd[0]);
            return {result, WIFEXITED(stat) ? WEXITSTATUS(stat) : -1};
        } else if (wait_result < 0) {
            close(pipefd[0]);
            return {result, -1};
        }
    }

    close(pipefd[0]);

    int stat;
    waitpid(pid, &stat, 0);
    return {result, WIFEXITED(stat) ? WEXITSTATUS(stat) : -1};
#endif
}

// CreatePipe/ClosePipe/ReadPipe/WritePipe/Dup2Pipe/ForkAndExec/WaitProcess
// are in pipe_handler_posix.cc / pipe_handler_win32.cc

bool Platform::SetPipeNonBlocking(int fd) {
    if (fd < 0) return false;
#ifndef _WIN32
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#else
    (void)fd;
    return false;
#endif
}

bool Platform::IsPipeWouldBlock() {
#ifndef _WIN32
    return errno == EAGAIN || errno == EWOULDBLOCK;
#else
    return false;
#endif
}

std::string Platform::GetPipeErrorString() {
#ifndef _WIN32
    return strerror(errno);
#else
    LPVOID buf = nullptr;
    DWORD err = GetLastError();
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                       nullptr, err, 0, (LPSTR)&buf, 0, nullptr) > 0) {
        std::string msg((LPSTR)buf);
        LocalFree(buf);
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
            msg.pop_back();
        }
        return msg;
    }
    return "Unknown error";
#endif
}

// ForkAndExec/WaitProcess are in pipe_handler_posix.cc / pipe_handler_win32.cc

ScriptResult Platform::ExecuteScriptWithTimeout(const std::string& script_path, int timeout_ms) {
    ScriptResult result;

    if (!PathExists(script_path)) {
        result.return_code = -1;
        result.error_output = "Script not found: " + script_path;
        return result;
    }

    std::string command = script_path;

#ifdef _WIN32
    if (script_path.size() >= 3 && script_path.substr(script_path.size() - 3) == ".py") {
        command = "python \"" + script_path + "\"";
    } else if (script_path.size() >= 3 && script_path.substr(script_path.size() - 3) == ".sh") {
        command = "bash \"" + script_path + "\"";
    }
#else
    std::filesystem::permissions(script_path,
        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec,
        std::filesystem::perm_options::add);
#endif

    int timeout_sec = timeout_ms / 1000;
    if (timeout_sec < 1) timeout_sec = 1;

    auto cmd_result = Platform::RunCommandWithOutput(command, timeout_sec);

    result.return_code = cmd_result.exit_code;
    result.output = cmd_result.output;

    if (cmd_result.exit_code == -2) {
        result.timeout = true;
        result.error_output = "Script timeout after " + std::to_string(timeout_ms) + "ms";
    }

    return result;
}



std::string Platform::BrowseForFile(const char* filter) {
#ifdef _WIN32
    char path[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter ? filter : "All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        return std::string(path);
    }
    return {};
#else
    (void)filter;
    return {};
#endif
}

std::string Platform::BrowseForDirectory() {
#ifdef _WIN32
    BROWSEINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.lpszTitle = "Select Sprite Assets Directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl != nullptr) {
        char path[MAX_PATH] = {0};
        if (SHGetPathFromIDListA(pidl, path)) {
            IMalloc* imalloc = nullptr;
            if (SUCCEEDED(SHGetMalloc(&imalloc))) {
                imalloc->Free(pidl);
                imalloc->Release();
            }
            return std::string(path);
        }
        IMalloc* imalloc = nullptr;
        if (SUCCEEDED(SHGetMalloc(&imalloc))) {
            imalloc->Free(pidl);
            imalloc->Release();
        }
    }
    return {};
#else
    return {};
#endif
}

bool Platform::OpenWithDefault(const std::string& path) {
#ifdef _WIN32
    HINSTANCE ret = ShellExecuteA(NULL, "open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return (intptr_t)ret > 32;
#elif __APPLE__
    std::string cmd = "open \"" + path + "\"";
    return system(cmd.c_str()) == 0;
#else
    std::string cmd = "xdg-open \"" + path + "\"";
    return system(cmd.c_str()) == 0;
#endif
}

bool Platform::TrashFile(const std::string& path) {
    if (!std::filesystem::exists(path)) return false;
    std::error_code ec;

#ifdef _WIN32
    // Use Shell32 SHFileOperation for recycle bin
    SHFILEOPSTRUCTA op = {};
    op.wFunc = FO_DELETE;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    std::string from = path + '\0';  // double-null terminated
    op.pFrom = from.c_str();
    return SHFileOperationA(&op) == 0;
#else
    // POSIX: try `trash` CLI first, fallback to ~/.trash
    std::string trash_cmd = "trash \"" + path + "\" 2>/dev/null";
    if (system(trash_cmd.c_str()) == 0) return true;

    const char* home = getenv("HOME");
    std::string trash_dir = std::string(home ? home : "/tmp") + "/.trash";
    std::filesystem::create_directories(trash_dir, ec);
    if (ec) return false;
    auto dest = std::filesystem::path(trash_dir) / std::filesystem::path(path).filename();
    std::filesystem::rename(path, dest, ec);
    return !ec;
#endif
}

bool Platform::IsAlreadyRunning() {
#ifdef _WIN32
    static HANDLE s_mutex = nullptr;
    if (s_mutex) return false;

    s_mutex = CreateMutexW(nullptr, FALSE, L"Prosophor-SingleInstance");
    if (!s_mutex) return false;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(s_mutex);
        s_mutex = nullptr;
        return true;
    }

    return false;
#else
    static int lock_fd = -1;
    if (lock_fd >= 0) return false;

    std::string lock_path;
    if (const char* home = getenv("HOME")) {
        lock_path = std::string(home) + "/.prosophor/prosophor.lock";
    } else {
        lock_path = "/tmp/prosophor.lock";
    }

    {
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(lock_path).parent_path(), ec);
    }

    lock_fd = open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (lock_fd < 0) return false;

    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        close(lock_fd);
        lock_fd = -1;
        return true;
    }

    // Write PID for diagnostic purposes
    std::string pid_str = std::to_string(getpid()) + "\n";
    // 诊断用 PID 写入;忽略短写失败(-Werror=unused-result)
    const ssize_t _ignored = write(lock_fd, pid_str.c_str(), pid_str.size());
    (void)_ignored;

    return false;
#endif
}

std::string Platform::ExpandEnv(const std::string& path) {
    std::string result = path;

    auto get_env = [](const char* var) -> std::string {
        char* val = std::getenv(var);
        return val ? std::string(val) : "";
    };

    auto expand_one = [&](const std::string& var, const std::string& val) {
        if (val.empty()) return;
        for (size_t pos = 0; (pos = result.find(var, pos)) != std::string::npos;) {
            result.replace(pos, var.length(), val);
            pos += val.length();
        }
    };

#ifdef _WIN32
    std::string localappdata = get_env("LOCALAPPDATA");
    std::string appdata = get_env("APPDATA");
    std::string userprofile = get_env("USERPROFILE");
    std::string temp = get_env("TEMP");
    std::string windir = get_env("WINDIR");

    auto norm = [](std::string& s) {
        for (auto& c : s) if (c == '/') c = '\\';
    };
    norm(localappdata); norm(appdata); norm(userprofile); norm(temp); norm(windir);

    expand_one("%LOCALAPPDATA%", localappdata);
    expand_one("%APPDATA%", appdata);
    expand_one("%USERPROFILE%", userprofile);
    expand_one("%TEMP%", temp);
    expand_one("%TMP%", temp);
    expand_one("%WINDIR%", windir);
#else
    std::string home = get_env("HOME");
    std::string xdg_cache_home = get_env("XDG_CACHE_HOME");
    if (xdg_cache_home.empty()) xdg_cache_home = home + "/.cache";

    expand_one("$HOME", home);
    expand_one("$XDG_CACHE_HOME", xdg_cache_home);
#endif

    return result;
}

std::vector<Platform::CacheDir> Platform::GetWellKnownCacheDirs() {
#ifdef _WIN32
    return {
        // -- System --
        {"Windows Temp",       "%TEMP%",                            "system"},
        {"Windows Prefetch",   "%WINDIR%\\Prefetch",                 "system"},
        {"Recycle Bin",        "%USERPROFILE%\\$Recycle.Bin",       "system"},
        {"Windows Update Cache", "%WINDIR%\\SoftwareDistribution\\Download", "system"},
        {"Windows Error Reporting", "%LOCALAPPDATA%\\CrashDumps",   "system"},
        {"Thumbnail Cache",    "%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer", "system"},
        {"Delivery Optimization", "%WINDIR%\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache", "system"},

        // -- Browsers --
        {"Chrome Cache",        "%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Cache", "browser"},
        {"Chrome Code Cache",   "%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Code Cache", "browser"},
        {"Chrome Service Worker", "%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Service Worker\\CacheStorage", "browser"},
        {"Chrome GPU Cache",    "%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\GPUCache", "browser"},
        {"Edge Cache",          "%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\Cache", "browser"},
        {"Edge Code Cache",     "%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\Code Cache", "browser"},
        {"Edge Service Worker", "%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\Service Worker\\CacheStorage", "browser"},
        {"Edge GPU Cache",      "%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\GPUCache", "browser"},
        {"Firefox Cache",       "%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles", "browser"},

        // -- Chat / Social --
        {"WeChat File Storage", "%USERPROFILE%\\Documents\\WeChat Files", "chat"},
        {"WeChat Plugin Cache", "%APPDATA%\\Tencent\\WeChat\\XPlugin", "chat"},
        {"QQ Cache",            "%APPDATA%\\Tencent\\QQ\\QtDataMgr", "chat"},
        {"QQ Light Pic Cache",  "%APPDATA%\\Tencent\\QQLight\\PicCache", "chat"},
        {"Discord Cache",       "%APPDATA%\\discord\\Cache", "chat"},
        {"Discord Code Cache",  "%APPDATA%\\discord\\Code Cache", "chat"},
        {"Discord GPU Cache",   "%APPDATA%\\discord\\GPUCache", "chat"},

        // -- IDEs --
        {"VS Code Cache",       "%APPDATA%\\Code\\Cache", "ide"},
        {"VS Code Cached Data", "%APPDATA%\\Code\\CachedData", "ide"},
        {"VS Code Cached Extensions", "%APPDATA%\\Code\\CachedExtensions", "ide"},
        {"VS Code VSIX Cache",  "%APPDATA%\\Code\\CachedExtensionVSIXs", "ide"},
        {"JetBrains Caches",    "%LOCALAPPDATA%\\JetBrains", "ide"},

        // -- Dev Tools --
        {"npm Cache",           "%APPDATA%\\npm-cache", "devtool"},
        {"pip Cache",           "%LOCALAPPDATA%\\pip\\cache", "devtool"},
        {"yarn Cache",          "%LOCALAPPDATA%\\Yarn\\Cache", "devtool"},
        {"NuGet Cache",         "%USERPROFILE%\\.nuget\\packages", "devtool"},
        {"Cargo Registry",      "%USERPROFILE%\\.cargo\\registry", "devtool"},
        {"Go Module Cache",     "%USERPROFILE%\\go\\pkg\\mod", "devtool"},
        {"CMake File API",      "%LOCALAPPDATA%\\CMake", "devtool"},
        {"Conan Cache",         "%USERPROFILE%\\.conan\\data", "devtool"},
    };
#else
    return {
        // -- System --
        {"Temp Dir",            "/tmp",                              "system"},
        {"Thumbnails",          "$HOME/.cache/thumbnails",           "system"},
        {"Trash",               "$HOME/.local/share/Trash",          "system"},

        // -- Browsers --
        {"Chrome Cache",        "$HOME/.cache/google-chrome",        "browser"},
        {"Chromium Cache",      "$HOME/.cache/chromium",             "browser"},
        {"Firefox Cache",       "$HOME/.cache/mozilla/firefox",      "browser"},

        // -- Chat / Social --
        {"Discord Cache",       "$HOME/.cache/discord",              "chat"},
        {"Slack Cache",         "$HOME/.cache/slack",                "chat"},

        // -- IDEs --
        {"VS Code Cache",       "$HOME/.config/Code/Cache",          "ide"},
        {"VS Code Cached Data", "$HOME/.config/Code/CachedData",     "ide"},
        {"JetBrains Caches",    "$HOME/.cache/JetBrains",            "ide"},

        // -- Dev Tools --
        {"npm Cache",           "$HOME/.npm/_cacache",               "devtool"},
        {"pip Cache",           "$HOME/.cache/pip",                  "devtool"},
        {"yarn Cache",          "$HOME/.cache/yarn",                 "devtool"},
        {"Cargo Registry",      "$HOME/.cargo/registry",             "devtool"},
        {"Go Module Cache",     "$HOME/go/pkg/mod",                  "devtool"},
        {"Conan Cache",         "$HOME/.conan/data",                 "devtool"},
    };
#endif
}

// ── 终止信号处理 ──────────────────────────────────────────────────────────

namespace {

std::function<void()> g_signal_cb;

#ifdef _WIN32
BOOL WINAPI ConsoleSignalHandler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT ||
        ctrl_type == CTRL_CLOSE_EVENT) {
        if (g_signal_cb) g_signal_cb();
        return TRUE;  // 已处理,阻止默认行为(直接终止)
    }
    return FALSE;
}
#else
void PosixSignalHandler(int /*signum*/) {
    if (g_signal_cb) g_signal_cb();
}
#endif

}  // namespace

void Platform::SetConsoleSignalHandler(std::function<void()> on_signal) {
    g_signal_cb = std::move(on_signal);
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleSignalHandler, TRUE);
#else
    std::signal(SIGINT, PosixSignalHandler);
    std::signal(SIGTERM, PosixSignalHandler);
#endif
}

std::string Platform::GetLanAddresses() {
    std::string result;
#ifdef _WIN32
    // 枚举网卡(GetAdaptersAddresses):不依赖主机名解析,直接拿各网卡 IPv4
    ULONG buflen = 0;
    if (GetAdaptersAddresses(AF_INET,
                             GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                 GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, nullptr, &buflen) != ERROR_BUFFER_OVERFLOW) {
        return result;
    }
    std::vector<char> buf(buflen);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_INET,
                             GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                 GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, adapters, &buflen) != NO_ERROR) {
        return result;
    }
    for (auto* a = adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            char ip[INET_ADDRSTRLEN] = {};
            if (!inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip))) continue;
            if (!result.empty()) result += ", ";
            result += ip;
        }
    }
    return result;
#else
    char hostname[256] = {};
    if (gethostname(hostname, sizeof(hostname) - 1) != 0) return result;
    struct addrinfo hints{};
    hints.ai_family = AF_INET;  // 只要 IPv4(局域网访问场景)
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &res) != 0) return result;
    for (auto* p = res; p; p = p->ai_next) {
        char ip[INET_ADDRSTRLEN] = {};
        auto* sa = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        if (!inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip))) continue;
        if (!result.empty()) result += ", ";
        result += ip;
    }
    freeaddrinfo(res);
    return result;
#endif
}

}  // namespace prosophor
