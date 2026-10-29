# Third-party components

Spumante itself is licensed under the **Apache License 2.0** (see [`LICENSE`](../../LICENSE)
and [`NOTICE`](../../NOTICE)). This directory holds the licenses of third-party code that
Spumante bundles or derives from.

## Wine (`wine/`)

Spumante is built on top of [Wine](https://www.winehq.org/) and ships a bundled Wine runtime
plus macOS parity overlays in `dlls/` and `tests/`. Those files are derived from Wine, so they
remain under Wine's license — the **GNU Lesser General Public License, version 2.1 or later**
(`LGPL-2.1-or-later`) — regardless of the Apache-2.0 license that covers the rest of the project.

- Full text: [`LICENSE-LGPL-2.1`](LICENSE-LGPL-2.1)
- Upstream: <https://gitlab.winehq.org/wine/wine>
- Copyright (c) 1993-2026 the Wine project authors.

Original Spumante macOS additions inside `dlls/` and `tests/` are dual-licensed as
`Apache-2.0 OR LGPL-2.1-or-later` so they can be used under either license.
