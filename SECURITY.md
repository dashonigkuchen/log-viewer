# Security policy

## Scope

Log Viewer is a local desktop application. It has **no network features**: no update checks, no
telemetry, no crash upload. It opens log files read-only. The most relevant risks are therefore
malformed or hostile input files (crashes, hangs, excessive memory use) and vulnerabilities in the
bundled third-party libraries (Qt, PCRE2, simdutf).

## Supported versions

Only the latest release receives fixes. Dependencies are updated automatically through Dependabot.

## Reporting a vulnerability

Please do **not** open a public issue. Use GitHub's private vulnerability reporting
("Security" tab → "Report a vulnerability") or contact the maintainer through GitHub.
Include the version (Help > About), the operating system, and, if possible, a sample file that
reproduces the problem. You can expect a first answer within two weeks.
