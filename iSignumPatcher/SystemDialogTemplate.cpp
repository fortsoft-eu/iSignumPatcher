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
 * Last modified for version 1.0.0.2
 */

#include "SystemDialogTemplate.h"

static const SIZE_T extendedDialogHeaderSize = 26;
static const SIZE_T extendedFontHeaderSize = 6;

/// Reads the window DPI supplied by the application.
UINT WindowDpi(HWND window);

/// Creates the system message font supplied by the application.
HFONT CreateUiFont(HWND window);

/// Advances past one variable-length dialog-template field while checking its bounds.
static bool SkipDialogField(const WORD*& cursor, const WORD* end) {
    if (cursor == end) {
        return false;
    }
    WORD value = *cursor++;
    if (value == 0xFFFF) {
        if (cursor == end) {
            return false;
        }
        ++cursor;
        return true;
    }
    while (value) {
        if (cursor == end) {
            return false;
        }
        value = *cursor++;
    }
    return true;
}

/// Converts the selected system message font to dialog-template point size at the owner DPI.
static UINT DialogFontPoints(HWND owner, HFONT font) {
    HDC dc = GetDC(owner);
    if (!dc) {
        return 0;
    }
    HGDIOBJ previous = SelectObject(dc, font);
    TEXTMETRICW metrics = {};
    BOOL measured = GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(owner, dc);
    return measured ? MulDiv(metrics.tmHeight - metrics.tmInternalLeading, 72, WindowDpi(owner)) : 0;
}

/// Copies an extended dialog template and embeds the current system message font.
DLGTEMPLATE* LoadSystemDialogTemplate(HINSTANCE instance, UINT resourceId, HWND owner) {
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_DIALOG);
    HGLOBAL loaded = resource ? LoadResource(instance, resource) : nullptr;
    DWORD size = resource ? SizeofResource(instance, resource) : 0;
    const BYTE* source = loaded ? static_cast<const BYTE*>(LockResource(loaded)) : nullptr;
    if (!source
            || size < extendedDialogHeaderSize
            || reinterpret_cast<const WORD*>(source)[1] != 0xFFFF
            || *reinterpret_cast<const DWORD*>(source + 12) & DS_SETFONT) {
        return nullptr;
    }
    const WORD* cursor = reinterpret_cast<const WORD*>(source + extendedDialogHeaderSize);
    const WORD* end = reinterpret_cast<const WORD*>(source + size);
    for (UINT index = 0; index < 3; ++index) {
        if (!SkipDialogField(cursor, end)) {
            return nullptr;
        }
    }
    SIZE_T fontOffset = reinterpret_cast<const BYTE*>(cursor) - source;
    SIZE_T controlsOffset = fontOffset + 3 & ~static_cast<SIZE_T>(3);
    if (controlsOffset > size) {
        return nullptr;
    }
    HFONT font = CreateUiFont(owner);
    LOGFONTW description = {};
    UINT points = font ? DialogFontPoints(owner, font) : 0;
    bool described = font && GetObjectW(font, sizeof(description), &description);
    if (font) {
        DeleteObject(font);
    }
    if (!described || !points || points >= 0x7FFF) {
        return nullptr;
    }
    SIZE_T faceBytes = (lstrlenW(description.lfFaceName) + 1) * sizeof(wchar_t);
    SIZE_T fontSize = extendedFontHeaderSize + faceBytes;
    SIZE_T newControlsOffset = fontOffset + fontSize + 3 & ~static_cast<SIZE_T>(3);
    SIZE_T newSize = newControlsOffset + size - controlsOffset;
    BYTE* copy = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, newSize));
    if (!copy) {
        return nullptr;
    }
    CopyMemory(copy, source, fontOffset);
    *reinterpret_cast<DWORD*>(copy + 12) |= DS_SETFONT;
    WORD* attributes = reinterpret_cast<WORD*>(copy + fontOffset);
    attributes[0] = static_cast<WORD>(points);
    attributes[1] = static_cast<WORD>(description.lfWeight);
    BYTE* flags = reinterpret_cast<BYTE*>(attributes + 2);
    flags[0] = description.lfItalic;
    flags[1] = description.lfCharSet;
    CopyMemory(copy + fontOffset + extendedFontHeaderSize, description.lfFaceName, faceBytes);
    CopyMemory(copy + newControlsOffset, source + controlsOffset, size - controlsOffset);
    return reinterpret_cast<DLGTEMPLATE*>(copy);
}
