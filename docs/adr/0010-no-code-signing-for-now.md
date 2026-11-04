# 0010. No code signing for now

- Status: Accepted
- Date: 2026-11-04

## Context
Unsigned executables trigger SmartScreen warnings. The owner uses the tool himself and may run unsigned portable executables at work.

## Decision
Releases are not code-signed for now. Executables carry version information and a manifest.

## Consequences
SmartScreen warnings on first start. When the repository is public, free signing through the SignPath Foundation program for open-source projects can be evaluated. Azure Artifact Signing is currently available to individual developers only in the USA and Canada; commercial certificates cost roughly EUR 200–400 per year.

## Alternatives considered
Self-signed certificate: does not help with SmartScreen.
