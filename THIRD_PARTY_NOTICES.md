# Third-party notices

## Unified Manager runtime

The self-contained Manager bundles Python (PSF license), Pillow (HPND license),
pyodbc (MIT), truststore (MIT), defusedxml (PSF license), pi-heif-decoder
(BSD-3-Clause), and resvg-py (MIT). Its executable uses the PyInstaller bootloader
(GPL with its distribution exception). Full license texts and the codec binary
provenance are bundled inside the EXE under `licenses/`.

HEIC/HEIF decoding uses the decoder-only pi-heif-decoder 1.7.0.post4 Windows
wheel. Its dynamically linked libheif 1.23.4 and libde265 1.1.3 libraries are
LGPL-3.0-or-later; their exact source archives are bundled with the notices. The
wheel has no x265 or other real encoder. Its MinGW runtime DLLs are distributed
under the GCC Runtime Library Exception and MinGW-w64 licenses recorded with the
codec notices.

Restricted SVG rasterization uses resvg-py 0.5.0. The MIT wrapper statically
contains its locked Rust dependency graph, including resvg/usvg 0.48.1; the
per-package license inventory and license files are bundled with the codec
notices. The build uses the pinned package versions and does not include game
resources or owner data.

The Windows/.NET framework used by the internal shop service is supplied by the
operating system and is not redistributed by this project.

## MinHook 1.3.4

Inline function hooking in `core.dll` uses MinHook (https://github.com/TsudaKageyu/minhook),
vendored unmodified in `third_party/minhook`.

* MinHook: Copyright (C) 2009-2017 Tsuda Kageyu. BSD 2-Clause License.
* Hacker Disassembler Engine 32 (part of MinHook): Copyright (c) 2008-2009 Vyacheslav Patkov.
  BSD 2-Clause License.

The full license text is `third_party/minhook/LICENSE.txt` (in the release package:
`licenses/MinHook-LICENSE.txt`).
