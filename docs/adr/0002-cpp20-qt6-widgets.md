# 0002. C++23 with Qt 6 Widgets

- Status: Accepted (amended 2026-11-10: C++20 → C++23)
- Date: 2026-11-04

## Context
The product must be as fast as the best native log viewers, run primarily on Windows 11, and be reviewable by the owner, who is a professional C++ developer. Agents write most of the code.

## Decision
C++23, Qt 6 Widgets (no QML), CMake with presets, Ninja. Log views are custom-painted `QAbstractScrollArea` widgets.

The project started on C++20 as a conservative default, not for a specific reason. It moved to C++23 once all three CI toolchains were verified (GCC 14, Clang 19, MSVC in Visual Studio 2026): `std::expected` replaces the `tl-expected` dependency, and the monadic operations of `std::optional`/`std::expected`, `std::ranges::to`, and deducing `this` are available. Only C++23 features supported by all three toolchains may be used; C++20 modules stay off.

## Consequences
Native performance and mature desktop behavior (selection, shortcuts, high DPI, accessibility). C++ needs strict guard rails for agents: warnings as errors, sanitizers, clang-tidy, a fixed build setup (see AGENTS.md). Library features that one toolchain lacks (for example `std::flat_map` before GCC 15) must not be used until all CI toolchains support them.

## Alternatives considered
- Rust + egui: excellent tooling for agents, permissive licenses, but the owner cannot review Rust yet and multi-line text selection in immediate-mode GUIs is weak.
- Electron/Tauri: rendering of millions of lines is not as smooth; larger footprint.
- C#/WPF (Windows only): not simpler, slower for this workload.
- Dear ImGui: fast, but poor text selection and IME support.
- Staying on C++20: no benefit; it kept an extra dependency and blocked useful library features.
