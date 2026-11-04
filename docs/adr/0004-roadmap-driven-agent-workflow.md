# 0004. Roadmap-driven agent workflow

- Status: Accepted
- Date: 2026-11-04

## Context
AI agents develop the project in many short sessions without shared memory. Each session must know what to do and how, from the repository alone.

## Decision
- `AGENTS.md` is the binding entry point; `docs/HANDOFF.md` carries state between sessions.
- `docs/ROADMAP.md` lists small steps with dependencies and acceptance criteria. One step = one branch = one PR.
- `docs/SPEC.md` holds requirement IDs; tests and steps reference them.
- Agents never merge unless the owner allows it in the session.

## Consequences
Predictable, reviewable progress; any agent tool can continue the work. Documents must be kept up to date in every PR, which costs some time per step.

## Alternatives considered
Free-form issues or one big plan document: harder for agents to find the next step and the definition of done.
