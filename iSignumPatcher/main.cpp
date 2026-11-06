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
 * Last modified for version 1.0.0.3
 */

#include "core.h"
#include "SelectionEdit.h"
#include "SystemDialogTemplate.h"
#include "resource.h"

/// Control dimensions and gaps read from the Windows-scaled dialog resource.
struct MainFormLayout {
    SIZE button;
    POINT margin;
    int buttonBottom;
    int editButtonGap;
};

static HWND editBox;
static HWND okButton;
static HWND sizeGrip;
static HWND mainWindow;
static MainFormLayout layout;
static BOOL running = TRUE;
static char inputPath[MAX_PATH];

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

/// Maps a native control rectangle into the parent dialog's client coordinates.
static RECT ControlRect(HWND control, HWND parent) {
    RECT rectangle = {};
    GetWindowRect(control, &rectangle);
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rectangle), 2);
    return rectangle;
}

/// Anchors the log, centered button and clipped size grip using the resource's native dimensions.
static void LayoutMainForm(HWND window) {
    if (!editBox || IsIconic(window)) {
        return;
    }
    RECT client = {};
    GetClientRect(window, &client);
    int buttonY = client.bottom - layout.buttonBottom - layout.button.cy;
    int editWidth = client.right - 2 * layout.margin.x;
    int editHeight = buttonY - layout.editButtonGap - layout.margin.y;
    SetWindowPos(editBox, nullptr, layout.margin.x, layout.margin.y,
        editWidth > 0 ? editWidth : 1, editHeight > 0 ? editHeight : 1, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(okButton, nullptr, (client.right - layout.button.cx) / 2, buttonY,
        layout.button.cx, layout.button.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    int gripWidth = GetSystemMetrics(SM_CXVSCROLL);
    int gripHeight = GetSystemMetrics(SM_CYHSCROLL);
    SetWindowPos(sizeGrip, HWND_BOTTOM, client.right - gripWidth, client.bottom - gripHeight,
        gripWidth, gripHeight, SWP_NOACTIVATE | (IsZoomed(window) ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

/// Uses the resource-created controls and retains their Windows-mapped sizes before processing starts.
static BOOL InitializeMainForm(HWND window) {
    mainWindow = window;
    editBox = GetDlgItem(window, IDC_LOG);
    okButton = GetDlgItem(window, IDOK);
    sizeGrip = GetDlgItem(window, IDC_SIZE_GRIP);
    if (!editBox || !okButton || !sizeGrip || !InstallSelectionEditHandler(editBox)) {
        return FALSE;
    }
    RECT client = {};
    GetClientRect(window, &client);
    RECT edit = ControlRect(editBox, window);
    RECT button = ControlRect(okButton, window);
    layout.button = SIZE{ button.right - button.left, button.bottom - button.top };
    layout.margin = POINT{ edit.left, edit.top };
    layout.buttonBottom = client.bottom - button.bottom;
    layout.editButtonGap = button.top - edit.bottom;
    SendMessageA(editBox, EM_SETLIMITTEXT, 65535, 0);
    EnableWindow(okButton, FALSE);
    LayoutMainForm(window);
    PostMessageA(window, WM_APP + 1, 0, 0);
    return TRUE;
}

/// Starts the existing worker and reports a native thread-creation failure in the log.
static void StartWorker(HWND window) {
    HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        logline("ERROR: The worker thread could not be started.");
        PostMessageA(window, WM_APP + 2, 0, 0);
    }
}

/// Enables normal dialog dismissal after the worker has completed.
static void CompleteProcessing(HWND window, WPARAM result) {
    running = FALSE;
    SetWindowTextA(window, result ? "iSignumPatcher - Completed" : "iSignumPatcher - Failed");
    EnableWindow(okButton, TRUE);
    SetFocus(okButton);
}

/// Closes the main dialog only after file processing has finished.
static void CloseMainForm(HWND window) {
    if (!running) {
        DestroyWindow(window);
    }
}

/// Handles the resource-based main dialog's lifecycle, resizing and worker notifications.
static INT_PTR CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
        case WM_INITDIALOG:
            if (!InitializeMainForm(window)) {
                DestroyWindow(window);
                return FALSE;
            }
            return TRUE;
        case WM_APP + 1:
            StartWorker(window);
            return TRUE;
        case WM_APP + 2:
            CompleteProcessing(window, wParam);
            return TRUE;
        case WM_SIZE:
            LayoutMainForm(window);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                CloseMainForm(window);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            CloseMainForm(window);
            return TRUE;
        case WM_DESTROY:
            PostQuitMessage(0);
            return TRUE;
    }
    return FALSE;
}

/// Starts the CRT-free application with the system-font dialog resource and Windows-selected position.
extern "C" void WINAPI Start() {
    DWORD length = GetModuleFileNameA(nullptr, inputPath, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        MessageBoxA(nullptr, "ERROR: The application path is too long.", "iSignum", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    char* last = nullptr;
    for (char* p = inputPath; *p; ++p) {
        if (*p == '\\') {
            last = p;
        }
    }
    if (!last || last - inputPath + 13 >= MAX_PATH) {
        ExitProcess(1);
    }
    lstrcpyA(last + 1, "iSignum.exe");
    HINSTANCE instance = GetModuleHandleW(nullptr);
    DLGTEMPLATE* definition = LoadSystemDialogTemplate(instance, IDD_MAIN, nullptr);
    if (!definition) {
        ExitProcess(1);
    }
    mainWindow = CreateDialogIndirectParamW(instance, definition, nullptr, proc, 0);
    HeapFree(GetProcessHeap(), 0, definition);
    if (!mainWindow || !IsWindow(mainWindow)) {
        ExitProcess(1);
    }
    RECT bounds = {};
    GetWindowRect(mainWindow, &bounds);
    HWND positionWindow = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, nullptr);
    if (positionWindow) {
        GetWindowRect(positionWindow, &bounds);
        DestroyWindow(positionWindow);
        SetWindowPos(mainWindow, nullptr, bounds.left, bounds.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(mainWindow, SW_SHOW);
    UpdateWindow(mainWindow);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(mainWindow, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    ExitProcess(0);
}