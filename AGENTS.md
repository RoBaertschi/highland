# Highland: Agent Notes

This repo is intentionally small and uses a "unity" build (one translation unit).

## Build

- Meson (preferred):
  - Configure: `meson setup builddir`
  - Build: `meson compile -C builddir`
  - Tests: none wired up currently (no `test(...)` targets in `meson.build`).
- Direct compile (quick smoke only):
  - Prefer Meson; the build generates Wayland protocol sources via `wayland-scanner`.

## Code Layout

- `src/highland.cpp` includes implementation files directly:
  - `#include "utils.cpp"`
  - `#include "arena.cpp"`
- This means IDE tooling may report missing identifiers when opening a `.cpp` file standalone.
  - Treat clangd/LSP errors in `src/arena.cpp` as potentially spurious unless the unity build fails.

## Wayland Protocol Codegen

- Meson generates server protocol sources from `wayland-protocols` XML using `wayland-scanner`.
- To add a protocol, edit `meson.build` and append to `wayland_protocols_xml`:
  - Format: `['name', join_paths('<group>', '<protocol>', '<protocol>.xml')]`
  - Example: `['xdg-shell', join_paths('stable', 'xdg-shell', 'xdg-shell.xml')]`
- Generated outputs (in `builddir/`):
  - `<name>-protocol.h`
  - `<name>-protocol.c`

## Style + Constraints

- Keep code close to C where reasonable.
  - Prefer plain functions + structs, minimal templates, minimal C++ features.
  - Avoid heavy abstractions, exceptions, RTTI, iostreams.
- When mapping enums/flags to strings, colors, or other small fixed metadata, prefer static tables + computed indices over long `switch`/`if` chains when it stays readable.
- Use project types: `isize`, `usize`, `u8`, etc.
- Use `ASSERT(...)` for invariants.
- Prefer ASCII source.

## Logging

- Logging uses `{fmt}` formatting (curly braces), not `printf`-style `%` specifiers.
  - Call sites should use `log_debugf("value {}", x)` etc.
- Colorized prefixes in `src/log.cpp` are enabled only when `isatty(STDERR_FILENO)`.
- `log_fatalf(...)` terminates the process with exit code 1.

## Arena / Memory Blocks

- Arena memory comes from `mmap` + `mprotect`.
  - Blocks start with a `Memory_Block` header stored inside the mapping.
  - Allocation is bump-pointer with alignment.
- `arena_reset_to(arena, to)`:
  - Frees all blocks except the initial one.
  - Zeros bytes beyond `to` within the remaining block.

## Making Changes

- Keep new APIs small and local unless needed elsewhere.
- If you add helpers in `src/utils.cpp`, prefer generic utilities that can be reused.
- Prefer the level-specific logging helpers (`log_debugf`, `log_infof`, `log_warningf`, `log_errorf`) over `logf(Log_Level::...)` at call sites.
- Logging prefixes are colorized in `src/log.cpp` only when `isatty(STDERR_FILENO)`; avoid embedding ANSI escapes in log message bodies.
- If you add a new `Log_Level`, keep `Log_Level::_Max` and the `labels[]` table ordering in `src/log.cpp` in sync.
- After changes, verify with: `meson compile -C builddir` (and `meson test -C builddir` if tests exist).
