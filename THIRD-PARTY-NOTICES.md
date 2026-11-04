# Third-party notices

Log Viewer is licensed under the MIT License (see `LICENSE`). The distributed application
contains or links the following third-party software. This file is shipped with every release.

## Qt 6

- Website: https://www.qt.io/
- License: GNU Lesser General Public License v3 (LGPL-3.0-only)
- Used modules: Qt Core, Qt Gui, Qt Widgets (and their platform plugins).
- Qt is linked **dynamically**. The Qt libraries are the `Qt6*.dll` files and the plugin
  directories next to `logviewer.exe`. You may replace them with your own build of the same Qt
  version (binary-compatible), as permitted by the LGPL.
- Source code of Qt: https://download.qt.io/official_releases/qt/ (the exact version is shown in
  Help > About). On request, the maintainers provide the corresponding source code as well.
- License text: https://www.gnu.org/licenses/lgpl-3.0.html

## PCRE2

- Website: https://github.com/PCRE2Project/pcre2
- License: BSD-3-Clause with the PCRE2 exception (JIT compiler: BSD-2-Clause)
- Linked statically.

## simdutf

- Website: https://github.com/simdutf/simdutf
- License: Apache-2.0 or MIT (used under MIT)
- Linked statically.

## tl::expected

- Website: https://github.com/TartanLlama/expected
- License: CC0-1.0 (public domain dedication)
- Header-only.

## Microsoft Visual C++ Runtime

- `msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll` are redistributed under the
  Microsoft Visual Studio redistribution terms.

## Build and test tools (not distributed)

Catch2 (BSL-1.0) and Google Benchmark (Apache-2.0) are used only to build tests and benchmarks.

> Keep this file in sync with `vcpkg.json` and `docs/ARCHITECTURE.md` §7. When a shipped
> dependency is added or removed, update this file in the same pull request.
