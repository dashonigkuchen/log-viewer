# 0013. English-only user interface

- Status: Accepted
- Date: 2026-11-04
- Supersedes: the "German translation of UI strings" part of 0001

## Context
The plan required an English and a German UI with Qt Linguist tooling and CI checks. The owner decided that the tool only needs an English UI and that translations add complexity without benefit.

## Decision
The UI is English only. There are no `.ts` files, no translation loading, no language setting, and no translation checks in CI. Plain string literals are fine; `tr()` may be used but is not required. Everything else in ADR 0001 (English code and docs, German chat and temporary plans) stays.

## Consequences
Less build tooling and fewer roadmap steps (M0-09 and M6-07 are dropped). Adding a language later would require wrapping all strings and introducing the tooling then.

## Alternatives considered
Keeping `tr()` everywhere for a possible future translation: rejected as a rule, because nobody plans to translate.
