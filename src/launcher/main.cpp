#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {

int find_free_port() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 54321; // fallback
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return 54321;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // OS allocates ephemeral free port

    int port = 54321;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        int len = sizeof(addr);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
            port = ntohs(addr.sin_port);
        }
    }

    closesocket(s);
    WSACleanup();
    return port;
}

std::wstring get_executable_directory() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::filesystem::path p(buf);
    return p.parent_path().wstring();
}

std::wstring find_server_executable() {
    const std::wstring base_dir = get_executable_directory();
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::path(base_dir) / "forge_server.exe",
        std::filesystem::path(base_dir) / "build" / "Release" / "forge_server.exe",
        std::filesystem::path(base_dir) / "Release" / "forge_server.exe",
        std::filesystem::path(base_dir) / "bin" / "forge_server.exe",
        std::filesystem::path(base_dir) / ".." / "build" / "Release" / "forge_server.exe"
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate.wstring();
        }
    }
    return L"";
}

bool wait_for_server_health(int port, int timeout_sec = 10) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    const auto start_time = std::chrono::steady_clock::now();
    bool ready = false;

    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time).count() < timeout_sec) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            addr.sin_port = htons(static_cast<u_short>(port));

            u_long mode = 1;
            ioctlsocket(s, FIONBIO, &mode);
            connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(s, &write_set);

            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100000; // 100ms

            if (select(0, NULL, &write_set, NULL, &tv) > 0) {
                // Socket connected
                std::string req = "GET /api/health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
                send(s, req.data(), static_cast<int>(req.size()), 0);

                char buf[512];
                int bytes = recv(s, buf, sizeof(buf) - 1, 0);
                if (bytes > 0) {
                    buf[bytes] = '\0';
                    if (std::string(buf).find("200 OK") != std::string::npos) {
                        ready = true;
                        closesocket(s);
                        break;
                    }
                }
            }
            closesocket(s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    WSACleanup();
    return ready;
}

void trigger_server_shutdown(int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(static_cast<u_short>(port));

        if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            std::string req = "POST /api/shutdown HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(s, req.data(), static_cast<int>(req.size()), 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        closesocket(s);
    }
    WSACleanup();
}

PROCESS_INFORMATION g_server_pi{};
int g_server_port = 0;
std::string g_url = "";

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT client_rect;
            GetClientRect(hwnd, &client_rect);

            // Dark instrument surface background (#0b1020)
            HBRUSH bg_brush = CreateSolidBrush(RGB(11, 16, 32));
            FillRect(hdc, &client_rect, bg_brush);
            DeleteObject(bg_brush);

            // Aurora gradient accent top bar
            RECT top_bar = {0, 0, client_rect.right, 5};
            HBRUSH bar_brush = CreateSolidBrush(RGB(6, 182, 212));
            FillRect(hdc, &top_bar, bar_brush);
            DeleteObject(bar_brush);

            SetBkMode(hdc, TRANSPARENT);

            // Title
            HFONT title_font = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                          DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            SelectObject(hdc, title_font);
            SetTextColor(hdc, RGB(233, 236, 255));
            TextOutA(hdc, 24, 24, "Forge Engineering Workbench", 28);
            DeleteObject(title_font);

            // Subtitle
            HFONT sub_font = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            SelectObject(hdc, sub_font);
            SetTextColor(hdc, RGB(140, 149, 214));
            TextOutA(hdc, 24, 54, "Windows 11 x64 Desktop Launcher (v1.0.0)", 40);

            // Status Card Box
            RECT status_box = {24, 85, client_rect.right - 24, 185};
            HBRUSH card_brush = CreateSolidBrush(RGB(18, 26, 51));
            FillRect(hdc, &status_box, card_brush);
            DeleteObject(card_brush);

            // Card details
            HFONT mono_font = CreateFontA(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH | FF_DONTCARE, "Consolas");
            SelectObject(hdc, mono_font);

            SetTextColor(hdc, RGB(52, 211, 153)); // green ok
            TextOutA(hdc, 38, 98, "[ONLINE]", 8);

            SetTextColor(hdc, RGB(233, 236, 255));
            std::string port_str = "Backend Port: " + std::to_string(g_server_port);
            TextOutA(hdc, 110, 98, port_str.c_str(), static_cast<int>(port_str.size()));

            std::string url_str = "URL: " + g_url;
            TextOutA(hdc, 38, 124, url_str.c_str(), static_cast<int>(url_str.size()));

            SetTextColor(hdc, RGB(140, 149, 214));
            TextOutA(hdc, 38, 152, "Press 'Open in Browser' or close this window to exit.", 52);

            DeleteObject(mono_font);
            DeleteObject(sub_font);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == 1001) { // Open Browser button
                ShellExecuteA(NULL, "open", g_url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            } else if (wmId == 1002) { // Stop & Exit button
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_server_port > 0) {
                trigger_server_shutdown(g_server_port);
            }
            if (g_server_pi.hProcess) {
                WaitForSingleObject(g_server_pi.hProcess, 1000);
                TerminateProcess(g_server_pi.hProcess, 0);
                CloseHandle(g_server_pi.hProcess);
                CloseHandle(g_server_pi.hThread);
            }
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow) {
    // 1. Locate forge_server executable
    std::wstring server_path = find_server_executable();
    if (server_path.empty()) {
        MessageBoxW(NULL,
                    L"Could not locate forge_server.exe in workspace or Release directory.\nEnsure project is built before running Launch.exe.",
                    L"Forge Launcher Error",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    // 2. Select an ephemeral free port
    g_server_port = find_free_port();
    g_url = "http://127.0.0.1:" + std::to_string(g_server_port);

    // 3. Spawn forge_server with hidden console
    std::wstring cmdline = L"\"" + server_path + L"\" --port " + std::to_wstring(g_server_port);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back(L'\0');

    std::wstring work_dir = get_executable_directory();

    if (!CreateProcessW(NULL, cmd_buf.data(), NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, work_dir.c_str(), &si, &g_server_pi)) {
        MessageBoxW(NULL, L"Failed to spawn forge_server background process.", L"Launch Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    // 4. Wait for server to pass health check
    if (!wait_for_server_health(g_server_port, 10)) {
        MessageBoxW(NULL, L"Forge server did not become ready within timeout.", L"Launch Error", MB_ICONERROR | MB_OK);
        trigger_server_shutdown(g_server_port);
        TerminateProcess(g_server_pi.hProcess, 1);
        return 1;
    }

    // 5. Open in browser immediately (Evergreen fallback)
    ShellExecuteA(NULL, "open", g_url.c_str(), NULL, NULL, SW_SHOWNORMAL);

    // 6. Create native Win32 controller window
    const wchar_t CLASS_NAME[] = L"ForgeWorkbenchLauncher";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Forge Engineering Workbench - Windows 11 x64",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 540, 260,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        return 1;
    }

    // Create "Open in Browser" button
    CreateWindowA("BUTTON", "Open in Browser",
                  WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                  24, 195, 140, 32,
                  hwnd, reinterpret_cast<HMENU>(1001), hInstance, NULL);

    // Create "Stop & Exit" button
    CreateWindowA("BUTTON", "Stop & Exit",
                  WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                  174, 195, 120, 32,
                  hwnd, reinterpret_cast<HMENU>(1002), hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
