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
 * Last modified for version 1.0.0.1
 */

#include "core.h"

struct Bits {
    const BYTE* p;
    DWORD n, pos, buf;
    int have;
    BOOL ok;
};

struct Huff {
    WORD count[16], symbol[288];
};

struct PE {
    BYTE* b;
    DWORD n, base, sec;
    WORD count;
};

/// Compares two byte sequences of length n.
static BOOL eq(const BYTE* a, const BYTE* b, DWORD n) {
    while (n--) {
        if (*a++ != *b++) {
            return FALSE;
        }
    }
    return TRUE;
}

/// Copies n bytes between nonoverlapping memory regions.
static void cp(BYTE* a, const BYTE* b, DWORD n) {
    while (n--) {
        *a++ = *b++;
    }
}

/// Fills a memory region with zero bytes.
static void zero(void* a, DWORD n) {
    BYTE* p = (BYTE*)a;
    while (n--) {
        *p++ = 0;
    }
}

/// Reads an unaligned 32-bit little-endian value.
static DWORD u32(const BYTE* p) {
    return p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

/// Reads an unaligned 16-bit little-endian value.
static WORD u16(const BYTE* p) {
    return (WORD)(p[0] | (p[1] << 8));
}

/// Calculates the ZIP-compatible CRC32 checksum of a byte sequence.
static DWORD crc(const BYTE* p, DWORD n) {
    DWORD c = ~0UL;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ ((0UL - (c & 1)) & 0xedb88320UL);
        }
    }
    return ~c;
}

/// Checks whether an offset and length fit within a buffer without overflow.
static BOOL range(DWORD p, DWORD n, DWORD size) {
    return p <= size && n <= size - p;
}

/// Allocates a byte buffer from the process heap. Returns null on failure.
static void* alloc(DWORD n) {
    return HeapAlloc(GetProcessHeap(), 0, n);
}

/// Releases a process-heap buffer. Accepts a null pointer.
static void release(void* p) {
    if (p) {
        HeapFree(GetProcessHeap(), 0, p);
    }
}

/// Reads n bits from a DEFLATE stream, least significant bit first.
/// Marks the stream invalid if the input ends before enough bits are available.
static unsigned bits(Bits* b, int n) {
    while (b->have < n) {
        if (b->pos == b->n) {
            b->ok = FALSE;
            return 0;
        }
        b->buf |= (DWORD)b->p[b->pos++] << b->have;
        b->have += 8;
    }
    unsigned v = b->buf & ((1UL << n) - 1);
    b->buf >>= n;
    b->have -= n;
    return v;
}

/// Builds a canonical Huffman decoding table from symbol code lengths.
/// Rejects invalid lengths and oversubscribed trees.
static BOOL tree(Huff* h, const BYTE* l, int n) {
    WORD off[16];
    zero(h, sizeof(*h));
    for (int i = 0; i < n; i++) {
        if (l[i] > 15) {
            return FALSE;
        }
        h->count[l[i]]++;
    }
    int left = 1;
    for (int k = 1; k <= 15; k++) {
        left = left * 2 - h->count[k];
        if (left < 0) {
            return FALSE;
        }
    }
    off[1] = 0;
    for (int k = 1; k < 15; k++) {
        off[k + 1] = off[k] + h->count[k];
    }
    for (int i = 0; i < n; i++) {
        if (l[i]) {
            h->symbol[off[l[i]]++] = (WORD)i;
        }
    }
    return TRUE;
}

/// Decodes one Huffman symbol, or returns -1 and marks the stream invalid.
static int symbol(Bits* b, Huff* h) {
    unsigned code = 0, first = 0, index = 0;
    for (int n = 1; n <= 15; n++) {
        code |= bits(b, 1);
        unsigned count = h->count[n];
        if (code >= first && code - first < count) {
            return h->symbol[index + code - first];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    b->ok = FALSE;
    return -1;
}

/// Decodes a raw RFC 1951 DEFLATE stream into a fixed-size output buffer.
/// Supports stored, fixed-Huffman, and dynamic-Huffman blocks and checks buffer bounds.
/// Returns TRUE only when input consumption and output length match the supplied sizes.
static BOOL inflate(const BYTE* src, DWORD ns, BYTE* out, DWORD no) {
    static const BYTE order[19] = {
        16,
        17,
        18,
        0,
        8,
        7,
        9,
        6,
        10,
        5,
        11,
        4,
        12,
        3,
        13,
        2,
        14,
        1,
        15
    };
    static const WORD lb[29] = {
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        13,
        15,
        17,
        19,
        23,
        27,
        31,
        35,
        43,
        51,
        59,
        67,
        83,
        99,
        115,
        131,
        163,
        195,
        227,
        258
    };
    static const BYTE le[29] = {
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        1,
        1,
        1,
        1,
        2,
        2,
        2,
        2,
        3,
        3,
        3,
        3,
        4,
        4,
        4,
        4,
        5,
        5,
        5,
        5,
        0
    };
    static const WORD db[30] = {
        1,
        2,
        3,
        4,
        5,
        7,
        9,
        13,
        17,
        25,
        33,
        49,
        65,
        97,
        129,
        193,
        257,
        385,
        513,
        769,
        1025,
        1537,
        2049,
        3073,
        4097,
        6145,
        8193,
        12289,
        16385,
        24577
    };
    static const BYTE de[30] = {
        0,
        0,
        0,
        0,
        1,
        1,
        2,
        2,
        3,
        3,
        4,
        4,
        5,
        5,
        6,
        6,
        7,
        7,
        8,
        8,
        9,
        9,
        10,
        10,
        11,
        11,
        12,
        12,
        13,
        13
    };
    Bits b = {
        src,
        ns,
        0,
        0,
        0,
        TRUE
    };
    DWORD pos = 0;
    BOOL last = FALSE;
    Huff h, d, c;
    BYTE lens[320];
    while (!last && b.ok) {
        last = bits(&b, 1);
        unsigned type = bits(&b, 2);
        if (type == 0) {
            b.buf = 0;
            b.have = 0;
            unsigned len = bits(&b, 16), inv = bits(&b, 16);
            if ((len ^ inv) != 65535 || len > no - pos) {
                return FALSE;
            }
            while (len--) {
                out[pos++] = (BYTE)bits(&b, 8);
            }
            continue;
        }
        if (type == 1) {
            for (int i = 0; i < 288; i++) {
                lens[i] = (BYTE)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
            }
            if (!tree(&h, lens, 288)) {
                return FALSE;
            }
            for (int i = 0; i < 32; i++) {
                lens[i] = 5;
            }
            if (!tree(&d, lens, 32)) {
                return FALSE;
            }
        } else if (type == 2) {
            unsigned nl = bits(&b, 5) + 257, nd = bits(&b, 5) + 1, nc = bits(&b, 4) + 4;
            if (nl > 286) {
                return FALSE;
            }
            zero(lens, 320);
            for (unsigned i = 0; i < nc; i++) {
                lens[order[i]] = (BYTE)bits(&b, 3);
            }
            if (!tree(&c, lens, 19)) {
                return FALSE;
            }
            unsigned i = 0;
            while (i < nl + nd && b.ok) {
                int s = symbol(&b, &c);
                if (s < 0) {
                    return FALSE;
                }
                if (s < 16) {
                    lens[i++] = (BYTE)s;
                } else {
                    unsigned repeat;
                    BYTE v = 0;
                    if (s == 16) {
                        if (!i) {
                            return FALSE;
                        }
                        v = lens[i - 1];
                        repeat = bits(&b, 2) + 3;
                    } else if (s == 17) {
                        repeat = bits(&b, 3) + 3;
                    } else {
                        repeat = bits(&b, 7) + 11;
                    }
                    if (repeat > nl + nd - i) {
                        return FALSE;
                    }
                    while (repeat--) {
                        lens[i++] = v;
                    }
                }
            }
            if (!lens[256] || !tree(&h, lens, nl) || !tree(&d, lens + nl, nd)) {
                return FALSE;
            }
        } else {
            return FALSE;
        }
        for (;;) {
            int s = symbol(&b, &h);
            if (!b.ok) {
                return FALSE;
            }
            if (s == 256) {
                break;
            }
            if (s < 256) {
                if (pos == no) {
                    return FALSE;
                }
                out[pos++] = (BYTE)s;
            } else {
                if (s > 285) {
                    return FALSE;
                }
                unsigned len = lb[s - 257] + bits(&b, le[s - 257]);
                int ds = symbol(&b, &d);
                if (ds < 0 || ds > 29) {
                    return FALSE;
                }
                unsigned dist = db[ds] + bits(&b, de[ds]);
                if (!b.ok || dist > pos || len > no - pos) {
                    return FALSE;
                }
                while (len--) {
                    out[pos] = out[pos - dist];
                    pos++;
                }
            }
        }
    }
    return b.ok && pos == no && b.pos == ns;
}

/// Validates an x86 PE32 header and initializes its section mapping.
static BOOL peinit(PE* p, BYTE* b, DWORD n) {
    if (n < 64 || u16(b) != 0x5a4d) {
        return FALSE;
    }
    DWORD e = u32(b + 60);
    if (!range(e, 248, n) || u32(b + e) != 0x4550 || u16(b + e + 4) != 0x14c || u16(b + e + 24) != 0x10b) {
        return FALSE;
    }
    WORD opt = u16(b + e + 20), count = u16(b + e + 6);
    if (opt < 224 || count == 0 || count > 96) {
        return FALSE;
    }
    DWORD sec = e + 24 + opt;
    if (!range(sec, (DWORD)count * 40, n)) {
        return FALSE;
    }
    p->b = b;
    p->n = n;
    p->base = u32(b + e + 52);
    p->sec = sec;
    p->count = count;
    return TRUE;
}

/// Maps a virtual address to a file offset, optionally requiring executable code.
/// Returns the maximum DWORD value when no valid mapping exists.
static DWORD offset(PE* p, DWORD va, BOOL code = FALSE) {
    if (va < p->base) {
        return ~0UL;
    }
    DWORD r = va - p->base;
    for (int i = 0; i < p->count; i++) {
        BYTE* s = p->b + p->sec + i * 40;
        DWORD start = u32(s + 12), size = u32(s + 16), raw = u32(s + 20);
        if (r >= start
                && r - start < size
                && (!code || (u32(s + 36) & 0x20000000) != 0)
                && range(raw, r - start + 1, p->n)) {
            return raw + r - start;
        }
    }
    return ~0UL;
}

/// Maps a file offset to a virtual address. Returns zero if no section contains it.
static DWORD address(PE* p, DWORD o) {
    for (int i = 0; i < p->count; i++) {
        BYTE* s = p->b + p->sec + i * 40;
        DWORD raw = u32(s + 20), size = u32(s + 16);
        if (o >= raw && o - raw < size) {
            return p->base + u32(s + 12) + o - raw;
        }
    }
    return 0;
}

/// Validates the AlphaSkins RTTI, setter, constructor, and patch signatures.
/// Returns 1 for a patchable file, 2 for an already patched file,
/// 3 for a known release without skins, or 0 for an unsupported or ambiguous file.
/// Sets ctor and setter to the patch offsets for states 1 and 2.
static int inspect(BYTE* b, DWORD n, DWORD* ctor, DWORD* setter) {
    PE p;
    if (!peinit(&p, b, n)) {
        return 0;
    }
    static const BYTE name[] = {
        7,
        13,
        'T',
        's',
        'S',
        'k',
        'i',
        'n',
        'M',
        'a',
        'n',
        'a',
        'g',
        'e',
        'r'
    };
    DWORD r = 0, hits = 0;
    for (DWORD i = 0; range(i, sizeof(name), n); i++) {
        if (eq(b + i, name, sizeof(name))) {
            r = i + sizeof(name);
            hits++;
        }
    }
    if (!hits) {
        DWORD c = crc(b, n);
        if (n == 7906424 && c == 0xeb98059fUL || n == 7989920 && c == 0x8fecefd6UL) {
            return 3;
        }
        return 0;
    }
    if (hits != 1 || !range(r, 11, n)) {
        return 0;
    }
    DWORD cv = u32(b + r), co = offset(&p, cv);
    if (co < 88 || co == ~0UL || u32(b + co - 88) != cv) {
        return 0;
    }
    DWORD q = r + 10;
    q += 1 + b[q];
    if (!range(q, 2, n)) {
        return 0;
    }
    WORD count = u16(b + q);
    q += 2;
    if (!count || count > 255) {
        return 0;
    }
    DWORD av = 0;
    hits = 0;
    for (int k = 0; k < count; k++) {
        if (!range(q, 27, n)) {
            return 0;
        }
        BYTE len = b[q + 26];
        if (!range(q + 27, len, n)) {
            return 0;
        }
        if (len == 6 && eq(b + q + 27, (const BYTE*)"Active", 6)) {
            if (u32(b + q + 4) != 0xff000040) {
                return 0;
            }
            av = u32(b + q + 8);
            hits++;
        }
        q += 27 + len;
    }
    if (hits != 1) {
        return 0;
    }
    DWORD so = offset(&p, av, TRUE);
    static const BYTE sig[] = {
        0x53,
        0x8b,
        0xd8,
        0x3a,
        0x53,
        0x40,
        0x74,
        0x7e,
        0x88,
        0x53,
        0x40,
        0x84,
        0xd2
    };
    if (!range(so, sizeof(sig), n)
            || !eq(b + so + 1, sig + 1, sizeof(sig) - 1)
            || b[so] != 0x53 && b[so] != 0xc3) {
        return 0;
    }
    DWORD ct = 0;
    hits = 0;
    for (DWORD i = 0; range(i, 24, n); i++) {
        if (b[i] != 0xc6 || b[i + 1] != 0x43 || b[i + 2] != 0x40 || b[i + 3] > 1) {
            continue;
        }
        static const BYTE a[] = {
            0x8b,
            0xcb,
            0xb2,
            1,
            0xa1
        };
        static const BYTE z[] = {
            0x89,
            0x83,
            0xf8,
            0,
            0,
            0
        };
        if (!eq(b + i + 4, a, 5) || b[i + 13] != 0xe8 || !eq(b + i + 18, z, 6)) {
            continue;
        }
        DWORD va = address(&p, i);
        if (va && av > va && av - va < 65536 && offset(&p, va, TRUE) == i) {
            ct = i;
            hits++;
        }
    }
    if (hits != 1) {
        return 0;
    }
    hits = 0;
    for (DWORD at = co; at - co < 256 && range(at, 4, n); at += 4) {
        DWORD o = offset(&p, u32(b + at), TRUE);
        if (o != ~0UL
                && o < ct
                && ct - o < 2048
                && range(o, 3, n)
                && b[o] == 0x55
                && b[o + 1] == 0x8b
                && b[o + 2] == 0xec) {
            hits++;
        }
    }
    if (hits != 1) {
        return 0;
    }
    *ctor = ct + 3;
    *setter = so;
    if (b[so] == 0xc3) {
        return b[ct + 3] == 0 ? 2 : 0;
    }
    return b[ct + 3] == 1 ? 1 : 0;
}

/// Reads a file of 64 bytes to 128 MiB into a process-heap buffer.
/// Returns null on failure. The caller must release a successful result.
static BYTE* readfile(const char* path, DWORD* n) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD hi = 0, len = GetFileSize(h, &hi), got = 0;
    BYTE* b = 0;
    if (!hi && len >= 64 && len <= 128UL * 1024 * 1024) {
        b = (BYTE*)alloc(len);
        if (b && (!ReadFile(h, b, len, &got, 0) || got != len)) {
            release(b);
            b = 0;
        }
    }
    CloseHandle(h);
    *n = len;
    return b;
}

/// Extracts an embedded application if present, verifies it, and disables custom styles.
/// Replaces the input file only after flushing and verifying the complete output.
/// Leaves payload and the temporary file path available for cleanup by the caller.
static BOOL patchLoadedFile(const char* path, LogFn log, BYTE* input, DWORD n, BYTE*& payload, char* temp) {
    DWORD size = n, zip = 0;
    BOOL packed = FALSE;
    for (DWORD i = 0; range(i, 30, n); i++) {
        if (u32(input + i) != 0x04034b50) {
            continue;
        }
        DWORD nl = u16(input + i + 26);
        if (nl != 11 || !range(i + 30, nl, n) || !eq(input + i + 30, (const BYTE*)"iSignum.exe", 11)) {
            continue;
        }
        zip = i;
        packed = TRUE;
        break;
    }
    if (packed) {
        log("INFO: Packed application detected. Extracting the embedded executable in memory.");
        DWORD start = zip + 30 + u16(input + zip + 26) + u16(input + zip + 28), cs = u32(input + zip + 18);
        size = u32(input + zip + 22);
        WORD method = u16(input + zip + 8);
        if ((u16(input + zip + 6) & 9) != 0
                || !size
                || size > 128UL * 1024 * 1024
                || !range(start, cs, n)
                || method != 0 && method != 8) {
            log("ERROR: The embedded archive format is not supported.");
            return FALSE;
        }
        payload = (BYTE*)alloc(size);
        if (!payload) {
            log("ERROR: Not enough memory to extract the application.");
            return FALSE;
        }
        if (method == 8 && !inflate(input + start, cs, payload, size) || method == 0 && cs != size) {
            log("ERROR: The embedded ZIP archive or DEFLATE data is invalid.");
            return FALSE;
        }
        if (method == 0) {
            cp(payload, input + start, size);
        }
        if (crc(payload, size) != u32(input + zip + 14)) {
            log("ERROR: The extracted application failed CRC verification.");
            return FALSE;
        }
        log("OK: Extraction and CRC verification completed successfully.");
    }
    DWORD ct = 0, st = 0;
    int state = inspect(payload, size, &ct, &st);
    if (!state) {
        log("ERROR: The application structure is unknown or ambiguous. No changes were written.");
        return FALSE;
    }
    if (state == 1) {
        log("OK: Delphi RTTI and the AlphaSkins Active property, setter, and constructor were verified.");
        payload[ct] = 0;
        payload[st] = 0xc3;
        DWORD c2 = 0, s2 = 0;
        if (inspect(payload, size, &c2, &s2) != 2) {
            log("ERROR: Patch verification failed. No changes were written.");
            return FALSE;
        }
        log("OK: Two bytes were patched. Custom styles are disabled and cannot be reactivated.");
        log("INFO: The modified application's digital signature will no longer be valid.");
    } else if (state == 2) {
        log("INFO: The application is already patched.");
    } else {
        log("INFO: A known older release without AlphaSkins was detected. No patch is required.");
    }
    if (state != 1 && !packed) {
        log("INFO: Nothing to do.");
        return TRUE;
    }
    char dir[MAX_PATH];
    lstrcpynA(dir, path, MAX_PATH);
    char* last = 0;
    for (char* s = dir; *s; s++) {
        if (*s == '\\' || *s == '/') {
            last = s;
        }
    }
    if (!last) {
        log("ERROR: The application directory could not be determined.");
        return FALSE;
    }
    last[1] = 0;
    if (!GetTempFileNameA(dir, "isp", 0, temp)) {
        log("ERROR: A temporary file could not be created in the application directory.");
        return FALSE;
    }
    HANDLE h = CreateFileA(temp, GENERIC_WRITE, 0, 0, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD wrote = 0;
    BOOL written = FALSE;
    if (h != INVALID_HANDLE_VALUE) {
        written = WriteFile(h, payload, size, &wrote, 0) && wrote == size && FlushFileBuffers(h);
        CloseHandle(h);
    }
    if (!written) {
        log("ERROR: The output file could not be written or flushed to disk. The original file was preserved.");
        return FALSE;
    }
    DWORD checkSize = 0;
    BYTE* check = readfile(temp, &checkSize);
    BOOL same = check && checkSize == size && eq(check, payload, size);
    release(check);
    if (!same) {
        log("ERROR: Output file verification failed. The original file was preserved.");
        return FALSE;
    }

    if (!MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        log("ERROR: The original file could not be replaced. It may be in use. The original file was preserved.");
        return FALSE;
    }
    temp[0] = 0;
    log("OK: Processing completed successfully. File \"iSignum.exe\" was replaced with the verified executable.");
    return TRUE;
}

/// Loads, extracts, and patches iSignum while reporting progress through log.
/// Releases all input and payload buffers and removes any remaining temporary file.
/// Returns TRUE on success, including when no changes are required.
BOOL PatchFile(const char* path, LogFn log) {
    DWORD n = 0;
    BYTE* input = readfile(path, &n);
    if (!input) {
        log("ERROR: File \"iSignum.exe\" could not be read. The file may be missing, locked, or too large.");
        return FALSE;
    }
    log("OK: File \"iSignum.exe\" was loaded.");
    BYTE* payload = input;
    char temp[MAX_PATH] = {
        0
    };
    BOOL ok = patchLoadedFile(path, log, input, n, payload, temp);
    if (temp[0]) {
        DeleteFileA(temp);
    }
    if (payload != input) {
        release(payload);
    }
    release(input);
    return ok;
}
