#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

bool IsServerRunning() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8555);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(s, &writeSet);
    timeval timeout{0, 200000};
    const int res = select(0, nullptr, &writeSet, nullptr, &timeout);
    closesocket(s);
    WSACleanup();
    return res > 0;
}

std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

std::wstring FindPythonExecutable() {
    const std::wstring standard = L"C:\\Program Files\\Autodesk\\3ds Max 2023\\Python\\python.exe";
    if (std::filesystem::exists(standard)) return standard;
    return L"python.exe";
}

bool LaunchServer(const std::filesystem::path& rootDir) {
    const auto python = FindPythonExecutable();
    const auto serverScript = rootDir / L"tools" / L"launcher" / L"server.py";
    std::wstring cmd = L"\"" + python + L"\" \"" + serverScript.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
    cmdBuffer.push_back(L'\0');
    const BOOL ok = CreateProcessW(
        nullptr,
        cmdBuffer.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        nullptr,
        rootDir.wstring().c_str(),
        &si,
        &pi
    );
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    return false;
}

int RunRelinkerDirectly(const std::filesystem::path& rootDir) {
    const auto relinker = rootDir / L"relinker.exe";
    if (!std::filesystem::exists(relinker)) {
        MessageBoxW(nullptr, L"relinker.exe not found in the root directory.", L"AnyPS5 Error", MB_ICONERROR);
        return 1;
    }
    std::wstring cmdLine = GetCommandLineW();
    const auto firstSpace = cmdLine.find(L' ');
    std::wstring args = (firstSpace != std::wstring::npos) ? cmdLine.substr(firstSpace + 1) : L"";
    std::wstring fullCmd = L"\"" + relinker.wstring() + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuffer(fullCmd.begin(), fullCmd.end());
    cmdBuffer.push_back(L'\0');
    if (!CreateProcessW(nullptr, cmdBuffer.data(), nullptr, nullptr, TRUE, 0, nullptr, rootDir.wstring().c_str(), &si, &pi)) {
        MessageBoxW(nullptr, L"Failed to start relinker process.", L"AnyPS5 Error", MB_ICONERROR);
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
}

}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const auto rootDir = GetExecutableDirectory();
    if (__argc > 1) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        return RunRelinkerDirectly(rootDir);
    }
    if (!IsServerRunning()) {
        LaunchServer(rootDir);
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (IsServerRunning()) break;
        }
    }
    ShellExecuteW(nullptr, L"open", L"http://127.0.0.1:8555", nullptr, nullptr, SW_SHOWNORMAL);
    return 0;
}
