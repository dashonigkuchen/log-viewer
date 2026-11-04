# 0011. Windows 11 primary platform and CI budget

- Status: Accepted
- Date: 2026-11-04

## Context
The owner uses Windows 11; agents work on Linux. The repository is private for now; GitHub Free includes a limited monthly Actions allowance, and the owner does not want to pay.

## Decision
Windows 11 x64 is the released platform; Linux x64 is built and tested in CI and used for development. Linux CI runs on every PR; Windows CI runs on `main` when sources change, on PRs labeled `windows`, nightly, and for releases. No payment method is configured, so exhausted minutes block CI instead of incurring cost.

## Consequences
Windows-specific bugs may be detected after merge; steps touching platform code require the `windows` label. When the repository becomes public, Windows CI can run on every PR.

## Alternatives considered
Self-hosted runner on the owner's machine: free, but the machine must be on and it needs maintenance.
