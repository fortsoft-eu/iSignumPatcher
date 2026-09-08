<#
This is open-source software licensed under the terms of the MIT License.

Copyright (c) 2026 Petr Červinka - FortSoft <cervinka@fortsoft.eu>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Last modified for version 1.0.0.0
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'

$b = [IO.File]::ReadAllBytes($Path)
$pe = [BitConverter]::ToInt32($b, 60)

if ([BitConverter]::ToUInt32($b, $pe) -ne 0x4550 -or
    [BitConverter]::ToUInt16($b, $pe + 4) -ne 0x14c -or
    [BitConverter]::ToUInt16($b, $pe + 24) -ne 0x10b) {
    throw 'Expected x86 PE32'
}

# Modern MS linkers reject old subsystem versions. The application uses no CRT.
# Set minimum OS and subsystem to 4.0 after linking (Windows NT 4.0 trial).
foreach ($o in @(($pe + 24 + 40), ($pe + 24 + 48))) {
    $b[$o] = 4
    $b[$o + 1] = 0
    $b[$o + 2] = 0
    $b[$o + 3] = 0
}

# Checksum is optional for an ordinary user-mode EXE.
foreach ($o in ($pe + 24 + 64)..($pe + 24 + 67)) {
    $b[$o] = 0
}

[IO.File]::WriteAllBytes($Path, $b)
