# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

EUI-NEO is a C++17 cross-platform desktop UI framework built on OpenGL and GLFW. It provides a declarative DSL for composing UI trees, a flexbox-inspired layout engine, animation system, and 25+ reusable components. Targets Windows, macOS, and Linux.

## Build Commands

```bash
# Configure (from project root)
cmake -B build

# Build all apps
cmake --build build

# Dependency mode options (bundled is default)
cmake -B build -DEUI_DEPS_MODE=bundled   # use vendored 3rd/
cmake -B build -DEUI_DEPS_MODE=auto      # find_package first, fallback to bundled
cmake -B build -DEUI_DEPS_MODE=fetch     # FetchContent from git
```

Each `.cpp` file in `app/` becomes a separate executable. Assets are auto-copied to the build output directory post-build.

## Architecture

### DSL Runtime (`core/`)

The framework centers on `core::dsl::Runtime` which manages composition, layout, animation, events, and rendering in a single-threaded loop. Key headers:

- **dsl.h / dsl_runtime.h** — `Ui` builder API and runtime. Elements are composed declaratively each frame; the runtime diffs structure changes and re-layouts only when needed.
- **layout.h** — Flexbox-style layout (Row, Column, Stack, Flow) with SizeMode (Fixed/WrapContent/Fill), flex grow/shrink, alignment, spacing.
- **primitive.h** — OpenGL rendering of rects, text, images, polygons (gradients, shadows, rounded corners, blur).
- **animation.h** — Property animations with easing functions and transitions.
- **event.h** — Pointer, keyboard, scroll, drag events with hit-testing and focus management.
- **text.h** — TrueType font rendering via stb_truetype with dynamic glyph atlas and UTF-8 support.
- **image.h** — PNG/SVG loading with caching; supports HTTP URLs with background fetching.
- **network.h** — HTTP text requests with caching (CURL-based, optional on Windows).
- **async.h** — Background task execution with cancellation tokens and `Result<T>`.
- **platform.h** — Platform-specific: URL opening, system tray, IME input.

### Components (`components/`)

All header-only, stateless builders following a consistent pattern:
```cpp
components::button(ui, id).text("Click").onClick(callback).build();
```

Components don't own rendering primitives or business state — they compose DSL elements and delegate state to the app layer. Theme support via color tokens (dark/light presets).

### Application Layer (`app/`)

Each app implements two functions:
```cpp
namespace app {
    const DslAppConfig& dslAppConfig();  // window config (title, size, fps, fonts, tray)
    void compose(core::dsl::Ui& ui, const core::dsl::Screen& screen);  // UI tree
}
```

The framework entry point is in `main.cpp` which manages the GLFW window lifecycle, DPI scaling, frame rate limiting, modal windows, and tray integration.

### Third-Party (`3rd/`)

Vendored: GLFW, glad, tray, stb_image, stb_truetype, nanosvg/nanosvgrast.

## Key Design Constraints

- **No exceptions or RTTI** — compiled with `-fno-exceptions -fno-rtti` (GCC/Clang) for small binaries.
- **Lazy rendering** — only redraws when state changes; uses `glfwWaitEvents` when idle.
- **Single-threaded rendering** — all GL calls on main thread; background work via async worker pool.
- **C99 for platform bridges** — `tray_bridge.c`, `ime_bridge.c` use C99 for Objective-C and system API interop.
