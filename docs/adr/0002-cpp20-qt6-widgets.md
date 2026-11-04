# 0002. C++20 with Qt 6 Widgets

- Status: Accepted
- Date: 2026-11-04

## Context
The product must be as fast as the best native log viewers, run primarily on Windows 11, and be reviewable by the owner, who is a professional C++ developer. Agents write most of the code.

## Decision
C++20, Qt 6 Widgets (no QML), CMake with presets, Ninja. Log views are custom-painted `QAbstractScrollArea` widgets.

## Consequences
Native performance and mature desktop behavior (selection, shortcuts, high DPI, accessibility). C++ needs strict guard rails for agents: warnings as errors, sanitizers, clang-tidy, a fixed build setup (see AGENTS.md).

## Alternatives considered
- Rust + egui: excellent tooling for agents, permissive licenses, but the owner cannot review Rust yet and multi-line text selection in immediate-mode GUIs is weak.
- Electron/Tauri: rendering of millions of lines is not as smooth; larger footprint.
- C#/WPF (Windows only): not simpler, slower for this workload.
- Dear ImGui: fast, but poor text selection and IME support.
