/**
 * This is open-source software licensed under the terms of the MIT License.
 *
 * Copyright (c) 2026 Petr Červinka - FortSoft <cervinka@fortsoft.eu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 **
 * Last modified for version 1.0.0.0
 */

#include "core.h"

#ifndef WM_XBUTTONDOWN
#define WM_XBUTTONDOWN 0x020B
#endif

#ifndef WM_XBUTTONDBLCLK
#define WM_XBUTTONDBLCLK 0x020D
#endif

static const UINT EDIT_CLICKS_TIMER = 1;

static HWND editBox;
static HWND okButton;
static HWND sizeGrip;
static HWND mainWindow;
static WNDPROC originalEditProc;
static BOOL running = TRUE;
static HWND lastClickedEdit;
static int editClickCount;
static POINT lastEditClickPoint;
static char inputPath[MAX_PATH];

/// Stops the edit click timer and clears the current click sequence.
static void resetEditClicks() {
    if (lastClickedEdit) {
        KillTimer(lastClickedEdit, EDIT_CLICKS_TIMER);
    }
    lastClickedEdit = 0;
    lastEditClickPoint.x = 0;
    lastEditClickPoint.y = 0;
    editClickCount = 0;
}

/// Selects the complete logical line, including wrapped rows but excluding CR and LF.
static void selectEditLine(HWND window) {
    int length = GetWindowTextLengthA(window);
    char* text = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length + 1);
    if (!text) {
        return;
    }
    length = GetWindowTextA(window, text, length + 1);
    DWORD start = 0, end = 0;
    SendMessageA(window, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    if (start > (DWORD)length) {
        start = length;
    }
    DWORD previousEnd = end > (DWORD)length ? length : end;
    end = start;
    while (start && text[start - 1] != '\r' && text[start - 1] != '\n') {
        --start;
    }
    while (end < (DWORD)length && text[end] != '\r' && text[end] != '\n') {
        ++end;
    }
    if (end < previousEnd) {
        end = previousEnd;
    }
    SendMessageA(window, EM_SETSEL, start, end);
    HeapFree(GetProcessHeap(), 0, text);
}

/// Handles Ctrl+A, normal Tab navigation, and CalClock-style triple-click selection.
/// Resets click tracking on timeout, focus loss, or another mouse button
/// and restores the original edit window procedure when the control is destroyed.
static LRESULT CALLBACK editProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_GETDLGCODE) {
        LRESULT result = CallWindowProcA(originalEditProc, window, message, wParam, lParam);
        if (wParam == VK_TAB) {
            result &= ~(DLGC_WANTTAB | DLGC_WANTALLKEYS);
        }
        MSG* key = (MSG*)lParam;
        if (key && (key->message == WM_KEYDOWN && key->wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0
            || key->message == WM_CHAR && key->wParam == 1)) {
            result |= DLGC_WANTMESSAGE;
        }
        return result;
    }
    if (message == WM_TIMER && wParam == EDIT_CLICKS_TIMER) {
        KillTimer(window, EDIT_CLICKS_TIMER);
        if (lastClickedEdit == window) {
            resetEditClicks();
        }
        return 0;
    }
    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK || message == WM_MBUTTONDOWN
        || message == WM_MBUTTONDBLCLK || message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK
        || message == WM_KILLFOCUS && lastClickedEdit == window) {
        resetEditClicks();
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
        if (lastClickedEdit != window) {
            resetEditClicks();
        }
        lastClickedEdit = window;
        KillTimer(window, EDIT_CLICKS_TIMER);
        BOOL deferPaint = editClickCount == 2 && IsWindowVisible(window);
        if (deferPaint) {
            SendMessageA(window, WM_SETREDRAW, FALSE, 0);
        }
        LRESULT result = CallWindowProcA(originalEditProc, window, message, wParam, lParam);
        DWORD start = 0, end = 0;
        SendMessageA(window, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        POINT point = {
            (short)LOWORD(lParam),
            (short)HIWORD(lParam)
        };
        int dx = point.x - lastEditClickPoint.x, dy = point.y - lastEditClickPoint.y;
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }
        if (end > start) {
            editClickCount = 2;
        } else if (editClickCount == 0 || dx < 2 && dy < 2) {
            ++editClickCount;
        } else {
            editClickCount = 0;
        }
        lastEditClickPoint = point;
        if (editClickCount == 3) {
            resetEditClicks();
            CallWindowProcA(originalEditProc, window, WM_LBUTTONUP, wParam & ~MK_LBUTTON, lParam);
            if (GetWindowLongA(window, GWL_STYLE) & ES_MULTILINE) {
                selectEditLine(window);
            } else {
                SendMessageA(window, EM_SETSEL, 0, -1);
            }
            SetFocus(window);
            result = 0;
        } else {
            SetTimer(window, EDIT_CLICKS_TIMER, GetDoubleClickTime(), 0);
        }
        if (deferPaint) {
            SendMessageA(window, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(window, 0, 0, RDW_INVALIDATE | RDW_FRAME);
        }
        return result;
    }
    if (message == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0
        || message == WM_CHAR && wParam == 1) {
        SendMessageA(window, EM_SETSEL, 0, -1);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        WNDPROC previous = originalEditProc;
        resetEditClicks();
        SetWindowLongA(window, GWL_WNDPROC, (LONG)previous);
        originalEditProc = 0;
        return CallWindowProcA(previous, window, message, wParam, lParam);
    }
    return CallWindowProcA(originalEditProc, window, message, wParam, lParam);
}

/// Appends a message and a CR/LF line ending to the read-only log control.
static void logline(const char* text) {
    SendMessageA(editBox, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageA(editBox, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageA(editBox, EM_REPLACESEL, FALSE, (LPARAM) "\r\n");
}

/// Runs file processing on a worker thread and posts the result to the main window.
static DWORD WINAPI worker(void*) {
    BOOL result = PatchFile(inputPath, logline);
    PostMessageA(mainWindow, WM_APP + 2, result, 0);
    return 0;
}

/// Creates and lays out the patcher controls and handles processing completion and closing.
static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        editBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL,
            6, 6, 600, 300, window, (HMENU)10, 0, 0);
        if (!editBox) {
            return -1;
        }
        originalEditProc = (WNDPROC)SetWindowLongA(editBox, GWL_WNDPROC, (LONG)editProc);
        if (!originalEditProc) {
            return -1;
        }
        SendMessageA(editBox, EM_SETLIMITTEXT, 65535, 0);
        okButton = CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            260, 312, 75, 23, window, (HMENU)IDOK, 0, 0);
        if (!okButton) {
            return -1;
        }
        sizeGrip = CreateWindowA("SCROLLBAR", "", WS_CHILD | WS_VISIBLE | SBS_SIZEGRIP | SBS_SIZEBOXBOTTOMRIGHTALIGN,
            0, 0, GetSystemMetrics(SM_CXVSCROLL), GetSystemMetrics(SM_CYHSCROLL), window, 0, 0, 0);
        if (!sizeGrip) {
            return -1;
        }
        SendMessageA(editBox, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SendMessageA(okButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        EnableWindow(okButton, FALSE);
        PostMessageA(window, WM_APP + 1, 0, 0);
        return 0;
    case WM_APP + 1:
    {
        HANDLE thread = CreateThread(0, 0, worker, 0, 0, 0);
        if (thread) {
            CloseHandle(thread);
        } else {
            logline("ERROR: The worker thread could not be started.");
            PostMessageA(window, WM_APP + 2, 0, 0);
        }
        return 0;
    }
    case WM_APP + 2:
        running = FALSE;
        SetWindowTextA(window, wParam ? "iSignumPatcher - Completed" : "iSignumPatcher - Failed");
        EnableWindow(okButton, TRUE);
        SetFocus(okButton);
        return 0;
    case WM_SIZE:
    {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        MoveWindow(editBox, 6, 6, width > 12 ? width - 12 : 1, height > 41 ? height - 41 : 1, TRUE);
        MoveWindow(okButton, (width - 75) / 2, height - 29, 75, 23, TRUE);
        int gripWidth = GetSystemMetrics(SM_CXVSCROLL);
        int gripHeight = GetSystemMetrics(SM_CYHSCROLL);
        MoveWindow(sizeGrip, width - gripWidth, height - gripHeight, gripWidth, gripHeight, TRUE);
        ShowWindow(sizeGrip, wParam == SIZE_MAXIMIZED || wParam == SIZE_MINIMIZED ? SW_HIDE : SW_SHOW);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK && !running) {
            DestroyWindow(window);
        }
        return 0;
    case WM_CLOSE:
        if (!running) {
            DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

/// Starts the CRT-free GUI application and resolves the adjacent iSignum.exe path.
/// Runs the window message loop and exits through the Windows API.
extern "C" void WINAPI Start() {
    DWORD length = GetModuleFileNameA(0, inputPath, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        MessageBoxA(0, "ERROR: The application path is too long.", "iSignum", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    char* last = 0;
    for (char* p = inputPath; *p; ++p) {
        if (*p == '\\') {
            last = p;
        }
    }
    if (!last || last - inputPath + 13 >= MAX_PATH) {
        ExitProcess(1);
    }
    lstrcpyA(last + 1, "iSignum.exe");
    WNDCLASSA cls = { 0 };
    cls.lpfnWndProc = proc;
    cls.hInstance = GetModuleHandleA(0);
    cls.hCursor = LoadCursorA(0, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    cls.lpszClassName = "iSignumPatcherWindow";
    if (!RegisterClassA(&cls)) {
        ExitProcess(1);
    }
    mainWindow = CreateWindowExA(0, cls.lpszClassName, "iSignumPatcher - Disable Custom Style",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 400, 200, 0, 0, cls.hInstance, 0);
    if (!mainWindow) {
        ExitProcess(1);
    }
    ShowWindow(mainWindow, SW_SHOW);
    UpdateWindow(mainWindow);
    MSG message;
    while (GetMessageA(&message, 0, 0, 0) > 0) {
        if (!IsDialogMessageA(mainWindow, &message)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
    ExitProcess(0);
}
