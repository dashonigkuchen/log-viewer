# 0003. Clean-room implementation under the MIT license

- Status: Accepted
- Date: 2026-11-04

## Context
The feature set is inspired by existing GPL-licensed log viewers (klogg, glogg). The owner wants a permissive license and a project that is fully his own.

## Decision
The project is licensed under MIT. Contributors (human and AI) must not read, copy, or paraphrase source code of other log viewers. Feature descriptions, user documentation, and screenshots of other tools are allowed. Generic algorithms from papers, textbooks, and library documentation are allowed. Dependencies must have permissive licenses; Qt is used under LGPL v3 and linked dynamically.

## Consequences
No license contamination. Some design work has to be done from first principles. `THIRD-PARTY-NOTICES.md` must be maintained.

## Alternatives considered
Forking klogg (GPL v3): instant features, but unmaintained upstream, dependencies pinned to commits in the author's forks (poor Dependabot support), legacy code, and low portfolio value.
