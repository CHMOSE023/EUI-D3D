---
name: eui-neo-dev
description: Use this skill when developing or modifying EUI-NEO apps, DSL pages, reusable components, or related runtime integrations in this repository. Covers app/*.cpp entry pages, components/*.h builders, core DSL usage, window/page flow, async tasks, network usage, and the project conventions AI developers should follow.
---

# EUI-NEO Developer Skill

This repository is a C++17 UI framework built on OpenGL + GLFW. The recommended development surface is:

- `app/*.cpp` for standalone app pages and demos
- `components/*.h` for reusable UI components
- `core/dsl.h` and `core::dsl::Runtime` as the only rendering/event/layout abstraction

Use this skill when the task is to build a page, add a component, extend an existing component, or wire app behavior on top of the current DSL.

## What This Project Is

- `main.cpp` owns the GLFW window loop, frame throttling, render scheduling, tray behavior, and multi-window lifecycle.
- `app/dsl_app.h` is the main app adapter. Most app work only needs `dslAppConfig()` and `compose(...)`.
- `components/` is a thin builder layer over the DSL. Components do not own OpenGL primitives directly.
- `core/` contains the real engine pieces: DSL, runtime, layout, events, animation, text, image, async, network, platform.
- `CMakeLists.txt` builds one executable per `app/*.cpp`.

## First Read

Before making substantial changes, read only the docs relevant to the request:

- App/page work: [窗口页面.md](窗口页面.md), [DSL.md](DSL.md), [布局.md](布局.md), [事件.md](事件.md)
- Component work: [组件.md](组件.md), [DSL.md](DSL.md), [事件.md](事件.md), [布局.md](布局.md)
- Async/network work: [异步.md](异步.md), [网络.md](网络.md)
- Visual/rendering edge cases: [动画.md](动画.md), [渲染流程.md](渲染流程.md), [图片.md](图片.md), [基础图元文本图元.md](基础图元文本图元.md)

Do not bulk-load all docs unless the task truly spans multiple layers.

## Core Mental Model

Write EUI-NEO in this direction:

1. App/page state lives in `app/*.cpp`, usually in an anonymous namespace.
2. `compose(core::dsl::Ui& ui, const core::dsl::Screen& screen)` declares the whole page from current state.
3. Components only compose DSL trees and emit callbacks with next values.
4. `core::dsl::Runtime` handles layout, hit-testing, focus, animation, dirty rects, and primitive sync.
5. Callbacks mutate page state, then Runtime decides whether to re-compose and re-render.

Do not write code as if this were an immediate-mode raw OpenGL app. The DSL is the source of truth.

## Non-Negotiable Conventions

- Keep element ids stable across recomposes.
- For component internals, derive child ids as `id + ".part"`.
- Prefer `#include "components/components.h"` in app pages.
- Keep components controlled whenever possible: page passes current value, callback returns next value.
- Do not let components own business state or hidden page lifecycle.
- Do not bypass Runtime by reading raw GLFW mouse state in app/component code.
- Do not create a second rendering abstraction beside the DSL.
- Prefer editing `app/` and `components/` first. Touch `core/` only when the task truly requires new framework capability.

## Repository Map

- `app/app.h`: app lifecycle interface used by `main.cpp`
- `app/dsl_app.h`: recommended app adapter and helpers like `openWindow(...)` and `app::async`
- `app/*.cpp`: each file is a standalone executable target
- `components/components.h`: aggregate export for component layer
- `components/theme.h`: theme tokens and visual helpers
- `core/dsl.h`: DSL builders and shared element properties
- `core/layout.h`: layout rules for `Row`, `Column`, `Stack`
- `core/event.h`: pointer, focus, text input, drag, scroll
- `core/async.*`: background work queue
- `core/network.*`: simple GET/text/image networking

## Building A New App Page

Create a new file under `app/`, then implement:

```cpp
#include "app/dsl_app.h"
#include "components/components.h"

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("My App")
        .pageId("my_app")
        .windowSize(1280, 800)
        .fps(90.0);
    return config;
}

void compose(core::dsl::Ui& ui, const core::dsl::Screen& screen) {
    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            // page tree
        })
        .build();
}

} // namespace app
```

Rules:

- `pageId` must be stable.
- Use `screen.width` and `screen.height` as logical size, not framebuffer pixel size.
- Put mutable page state in an anonymous namespace in the same `app/*.cpp`.
- Let callbacks mutate that state directly.
- Use `components::theme` tokens when the page needs a coherent visual system.
- Prefer layout-first page composition: build sections with `row`, `column`, `stack`, `gap`, `margin`, `align`, `fill`, and `wrapContent` before reaching for manual `.x(...)` / `.y(...)` math.
- Prefer `flow`, `padding`, `min/max`, and `flex` before introducing manual width formulas for responsive groups.

## Building Or Extending A Component

Most components in this repo are header-only builders in `components/*.h`.

Follow this shape:

1. Define a `Style` struct seeded from `components::theme::ThemeColorTokens`.
2. Define a `Builder` class storing config and callbacks.
3. Compose only DSL nodes in `build()`.
4. Expose a free function like `inline ButtonBuilder button(core::dsl::Ui& ui, const std::string& id)`.
5. Export the header from `components/components.h`.

Use existing files as reference patterns:

- `components/button.h` for `Stack + Rect + Text` composition and press-scale following
- `components/checkbox.h` for controlled boolean state
- `components/input.h` for focus/text-input handling
- `components/scroll.h` for controlled scrolling behavior
- `components/scrollview.h` for auto-measured scrollable containers

## Component Authoring Rules

- Internal background `Rect` usually owns hover/pressed visuals and click handling.
- If the whole component should scale or fade with a child visual state, use `.visualStateFrom(...)` or `.hoverOpacityFrom(...)` on an outer container.
- Prefer `.states(normal, hover, pressed)` on `Rect` instead of hand-rolling hover logic.
- If a component exposes style overrides, support both `.style(...)` and `.theme(...)` when appropriate.
- Public API names should follow the repo’s current style such as `datepicker`, `timepicker`, `toggleSwitch`, `dataTable`.
- Keep geometry math local to `build()`, and keep it deterministic from current builder fields.

Avoid these mistakes:

- Storing app business state inside the component
- Reading raw input devices directly
- Creating unstable ids from changing text/value content
- Making components depend on global mutable singletons unless the repo already uses that pattern for the same problem

## Easy-To-Miss UI Pitfalls

- In this DSL, text that should look vertically centered usually needs `verticalAlign(core::VerticalAlign::Center)` plus a `lineHeight(...)` near the actual font size. Do not blindly set `lineHeight` to the full control height, or the text can look visually top-biased even when the frame is centered.
- For scrollable pages like `app/gallery.cpp`, prefer `components::scrollView(...)` or a layout measurement pass instead of maintaining magic height sums by hand. If width changes when a scrollbar appears, do a second measurement pass with the reduced content width.
- `flow` is the default choice for responsive button groups, chip rows, picker rows, and property-card grids. Do not manually wrap rows with ad-hoc `if (x > width)` math in app code.
- Use `padding(...)` for container insets first. Do not create extra wrapper layers or hand-subtract inner widths unless the component API truly requires it.
- Use `minWidth/maxWidth/flexGrow/flexShrink` to express responsive intent before writing repeated `std::min/std::max` width formulas in pages.

## DSL Rules That Matter In Practice

Available primitives and containers are:

- Containers: `row`, `column`, `stack`, `flow`
- Visual primitives: `rect`, `text`, `image`, `polygon`

Important shared builder capabilities from `core/dsl.h`:

- Layout: `.x()`, `.y()`, `.size()`, `.margin()`, `.gap()`, `.align()`, `.clip()`, `.zIndex()`
- Interaction: `.interactive()`, `.onClick()`, `.focusable()`, `.onFocusChanged()`, `.onTextInput()`, `.onScroll()`, `.onDrag()`
- Motion: `.transition(...)`, `.animate(...)`, transform properties

Important current layout capabilities:

- `padding(...)` is available on layout builders and participates in measure/layout.
- `flow(...)` is available for auto-wrapping horizontal groups.
- `minWidth/minHeight/maxWidth/maxHeight` are available on layout builders.
- `Fill` plus `flexGrow/flexShrink` supports lightweight main-axis space distribution.
- Transform still does not change hit-test bounds.

Prefer layout over manual positioning whenever possible.

- Default to `row` / `column` / `stack` plus `gap`, `margin`, `align`, `fill`, and `wrapContent`.
- For responsive horizontal groups, default to `flow` before inventing breakpoint math.
- Use explicit `.x(...)` / `.y(...)` mainly for overlay content, free-positioned decoration, drag targets, or intentionally absolute `stack` children.
- If you find yourself hand-calculating many sibling positions or content heights in app code, first ask whether another nested layout container can express it.
- If a scrollable page needs content height, prefer `components::scrollView` or measuring the DSL layout result instead of maintaining hard-coded height sums.

### Common Layout Choices

- Use `row` for a single horizontal line that should not wrap.
- Use `flow` for horizontal groups that should wrap naturally as width changes.
- Use `column` for vertical sections and stacked form rows.
- Use `stack` for overlays, decorations, floating layers, and absolute-positioned children.
- Use `padding` on the container before subtracting content width by hand.
- Use `min/max/flex` before locking child widths to viewport-derived formulas.

Use nested containers and margins instead of inventing a new layout system.

## State And Interaction Patterns

Preferred page pattern:

- Anonymous-namespace state in `app/*.cpp`
- UI reads state during `compose(...)`
- Callbacks write next state
- Runtime re-composes as needed

Preferred control pattern:

```cpp
components::slider(ui, "volume")
    .value(volume)
    .onChange([&](float next) {
        volume = next;
    })
    .build();
```

Use callbacks instead of imperative mutation of primitives.

For click/hover visuals:

- Put `.states(...)` on a `Rect`
- Put `.transition(...)` on the nodes that should animate
- Use outer `Stack` wrappers when the visual effect needs to coordinate multiple children

## Windows, Tray, And Multi-Page Apps

When a task requires extra windows, use `app::openWindow(...)` from `app/dsl_app.h`.

- Use `DslWindowConfig` for title, page id, size, background, modal behavior.
- Use `.modal(true)` only when the main window should stop handling normal input while the child is active.
- Tray behavior is opt-in through `DslAppConfig{}.tray(true)`.

Do not add a second custom window loop unless the task explicitly requires framework-level work.

## Async And Network

For background tasks, prefer `app::async` from `app/dsl_app.h`.

- `runOnce(...)` for one-shot loading
- `restart(...)` for refresh/search/retry flows
- `cancel(...)` for cooperative cancellation

For simple text or image GET requests, reuse `core::network`.

Rules:

- Background work should finish by updating page state in the completion callback.
- Do not block `compose(...)`, `update(...)`, or event callbacks with long-running work.
- Reuse the existing async/network abstractions before inventing new task queues or HTTP wrappers.

## When To Touch `core/`

Change `core/` only if the task cannot be solved cleanly at the app/component layer, for example:

- A missing DSL builder capability
- A missing runtime interaction feature
- A layout rule change
- A new primitive/rendering behavior that multiple components need

If you modify `core/`, inspect the relevant docs and neighboring code first, because changes there affect every app target.

## Validation Workflow

After changes:

1. Re-read the edited app/component and verify ids remain stable.
2. Check that callbacks only mutate state and do not bypass Runtime.
3. Confirm new component headers are exported from `components/components.h` if needed.
4. Build the affected app target.
5. If the change is visual or interactive, run the target and inspect the actual UI behavior.

Build commands:

```bash
cmake -S . -B build
cmake --build build --config Release
```

Target naming rule:

- `app/gallery.cpp` -> `gallery`
- `app/demo.cpp` -> `demo`
- `app/my_app.cpp` -> `my_app`

## Good Local References

- `app/demo.cpp`: smallest DSL page example
- `app/gallery.cpp`: best broad reference for components, themes, animation, network, and composed layouts
- `app/clock.cpp`: custom app styling and large page composition
- `app/serial_tool.cpp`: tray-enabled app and dashboard-style composition
- `components/button.h`: canonical builder style
- `components/input.h`: most complex interaction-heavy component

## Default Strategy For AI Developers

When implementing a feature in this repo, default to this order:

1. Understand whether the request is app-level, component-level, or framework-level.
2. Read only the relevant docs and one or two nearby code examples.
3. Make the smallest change in `app/` or `components/` that fits existing conventions.
4. Touch `core/` only if the current DSL/runtime truly lacks the required capability.
5. Build and verify the specific affected target.

If multiple approaches are possible, prefer the one that keeps the DSL tree declarative, component APIs controlled, and Runtime responsibilities centralized.
