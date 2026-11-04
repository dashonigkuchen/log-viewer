# 0012. aqtinstall pinned to an unreleased fix commit

- Status: Accepted
- Date: 2026-11-04

## Context
Qt 6.12 changed the layout of its online repository. The latest aqtinstall release (3.3.0) fails on Windows with "Failed to locate XML data for Qt version '6.12.0'" ([miurahr/aqtinstall#1047](https://github.com/miurahr/aqtinstall/issues/1047)). The fix is in the open pull request [miurahr/aqtinstall#1048](https://github.com/miurahr/aqtinstall/pull/1048). Falling back to Qt 6.11 is not an option: 6.11 reached the end of its support on 2026-09-30, while 6.12 is the current LTS line.

## Decision
CI installs aqtinstall from the fix commit, pinned by its full SHA (`git+https://github.com/jdpurcell/aqtinstall@6f0f842da47c744cdc814cd94da9828d934d7bf0`), through the `aqtsource` input of `jurplel/install-qt-action`. The pin is set in `.github/actions/setup-linux/action.yml` and `.github/workflows/ci-windows.yml`. It was verified locally that this commit installs `qtbase` 6.12.0 for `win64_msvc2022_64`.

## Consequences
- A commit SHA is immutable, so the build is reproducible; the code comes from a fork that is not an official release.
- As soon as an aqtinstall release contains the fix, both pins are removed (the action's default `aqtversion` is used again) and this ADR is superseded. The nightly workflow (M0-08) checks this.

## Alternatives considered
- Qt 6.11.3: works with aqtinstall 3.3.0, but is out of support.
- Building Qt from source or using the Qt online installer on CI: much slower, uses many CI minutes.
