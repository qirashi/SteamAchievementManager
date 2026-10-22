/* Copyright (c) 2026 Qirashi
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would
 *    be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not
 *    be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution.
 */

// SAM.Game copies this program under the executable name of a game and runs
// it, so Discord detects the game as being played. Discord only picks up
// processes that have a window, so it opens one (minimized, titled with the
// game's name, from SAM_WINDOW_TITLE) and keeps it until SAM.Game (whose
// process id is in SAM_PARENT_PID) exits or the window is closed. The command
// line is left for the arguments Discord may look for.

#include <windows.h>

#include <cwchar>

namespace
{
const wchar_t kClassName[] = L"SAM.Stub";
const wchar_t kMessage[] = L"Steam Achievement Manager shows this game in Discord while this window is open.\n"
                           L"Close it to stop.";

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            RECT rect;
            GetClientRect(window, &rect);
            InflateRect(&rect, -16, -16);
            SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, kMessage, -1, &rect, DT_WORDBREAK);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    wchar_t buffer[16];
    const DWORD length = GetEnvironmentVariableW(L"SAM_PARENT_PID", buffer, 16);
    if (length == 0 || length >= 16)
    {
        return 1;
    }
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, std::wcstoul(buffer, nullptr, 10));
    if (parent == nullptr)
    {
        return 1;
    }

    wchar_t title[256];
    const DWORD titleLength = GetEnvironmentVariableW(L"SAM_WINDOW_TITLE", title, 256);
    if (titleLength == 0 || titleLength >= 256)
    {
        GetModuleFileNameW(nullptr, title, 256);
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);
    HWND window = CreateWindowExW(0,
                                  kClassName,
                                  title,
                                  WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  420,
                                  140,
                                  nullptr,
                                  nullptr,
                                  instance,
                                  nullptr);
    if (window == nullptr)
    {
        CloseHandle(parent);
        return 1;
    }
    ShowWindow(window, SW_SHOWMINNOACTIVE);

    int result = 0;
    for (bool running = true; running;)
    {
        const DWORD wait = MsgWaitForMultipleObjects(1, &parent, FALSE, INFINITE, QS_ALLINPUT);
        if (wait != WAIT_OBJECT_0 + 1)
        {
            break; // SAM.Game exited (or the wait failed)
        }
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                result = static_cast<int>(message.wParam);
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CloseHandle(parent);
    return result;
}
