# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build commands

```sh
make          # build (clang, outputs ./Launcher binary)
make run      # build and run
make clean    # remove build/ and binary
make test     # print computed object file list (makefile debug util)
```

The compiler is `clang` with `-Wall -g`. All `.c` files under `src/` are picked up automatically; object files go to `build/` mirroring the `src/` tree.

## Architecture

This is a Linux app launcher written in C, mimicking the GNOME/Android grid launcher style. The codebase has two separate "main" entry points at different stages of development:

- **`src/main.c`** — the current active entry point (wired into the makefile). It exercises the parsing subsystems and serves as a development harness.
- **`main.c`** (root) — the planned SDL3 + Clay UI frontend. It uses the `SDL_MAIN_USE_CALLBACKS` pattern (SDL3, SDL3_ttf, Clay). **Not yet wired into the build**; it will need SDL3/SDL3_ttf link flags added to `libFlags` in the makefile.

### Parsing subsystems (`src/`)

All parsers are built on a shared INI-parsing primitive and use **DFA transition tables** (the `E(state,char,next_state)` macro) to identify keys in a single pass without `strcmp`. Negative states are terminal and map directly to field indices via `-(state+1)`.

| File | Role |
|---|---|
| `iniFileParsingUtils` | Core INI parser: reads `[Section]` headers and dispatches key values via a caller-supplied `iniSectionDef` (transition table + endings array + handler callback). |
| `desktopFileParser` | Parses `.desktop` files (XDG spec) from one or more directories into `desktopFileList → desktopFile → desktopGroup` structs. Reads from `/usr/share/applications/`. |
| `iconIndexParser` | Parses `index.theme` files into `iconIndexFile` + `iconIndexDir` structs describing the theme's subdirectory layout. |
| `iconFinder` | Walks a chain of icon themes (requested theme → `hicolor` → fallback) and implements the XDG icon lookup algorithm (`DirectorySizeDistance`) to find the best-matching icon path. |

### UI layer (`clay/`, `main.c`)

Uses [Clay](https://github.com/nicbarker/clay) (single-header, `clay/clay.h`) with the SDL3 renderer (`clay/SDL3/clay_renderer_SDL3.c`). The layout function (`ClayImageSample_CreateLayout`) is called each frame from `SDL_AppIterate`. Press `Space` to toggle Clay's built-in debug overlay.

### Known TODOs in the code

- `iconFinder.c`: theme inheritance (`Inherits` field) is parsed but not yet recursed.
- `iconFinder.c`: fallback icon directory is not yet populated (`addFallbackIconDir` is a stub).
- `iconIndexParser.c`: icon path construction appends the dir name onto the full `index.theme` file path instead of the theme root — this is a bug (the path will be wrong).
- Locale strings and array-valued fields in `.desktop` files are read as raw strings; no splitting or locale resolution is implemented.
