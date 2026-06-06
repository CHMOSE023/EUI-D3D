#include "dsl_app.h"
#include "components/components.h"

#include <algorithm>
#include <cmath>

// ============================================================
//  App
// ============================================================
namespace app {

    const DslAppConfig& dslAppConfig() {
        static const DslAppConfig config = DslAppConfig{}
            .title("3D Cube Component Demo")
            .pageId("demo")
            .clearColor({ 0.13f, 0.15f, 0.18f, 1.0f })
            .windowSize(800, 600);
        return config;
    }

    void compose(core::dsl::Ui& ui, const core::dsl::Screen& screen) {

        // ---- Persistent state (static = survives across render frames) ----
        // compose() is only called once (first frame) and on window resize;
        // all subsequent visual updates happen via callbacks in update().
        // So cube state must be the single source of truth, updated in-place
        // by onDrag / onScroll / onTimer — NOT re-assigned from a ViewState here.
#ifdef EUI_D3D11
        static components::Cube3DState cube;       // D3D11 resources + yaw/pitch/zoom
#endif
        static bool everInteracted = false;        // auto-spin until first touch

        const float side    = std::min(screen.width, screen.height) * 0.68f;
        const float canvasX = (screen.width  - side) * 0.5f;
        const float canvasY = (screen.height - side) * 0.5f;

        // ---- Build UI tree ----
        ui.stack("root")
            .size(screen.width, screen.height)
            .content([&] {

            // Dark background
            ui.rect("bg")
                .size(screen.width, screen.height)
                .color({ 0.13f, 0.15f, 0.18f, 1.0f })
                .build();

            // Title
            components::text(ui, "title")
                .position(0.0f, 22.0f)
                .size(screen.width, 34.0f)
                .text("3D Cube   —   components::cube3d")
                .fontSize(22.0f)
                .lineHeight(34.0f)
                .color({ 0.94f, 0.97f, 1.0f, 1.0f })
                .horizontalAlign(core::HorizontalAlign::Center)
                .build();

#ifdef EUI_D3D11
            // ---- 3D cube component ----
            //
            // All callbacks fire during update() (before render()), so mutations
            // to `cube` are visible in the very same frame's render pass.
            // `cube` and `everInteracted` are static locals → accessible from
            // capture-less [] lambdas (static-duration variables need no capture).
            components::cube3d(ui, "cube", cube)
                .position(canvasX, canvasY)
                .size(side, side)
                // Left-drag → rotate
                .onDrag([](const core::dsl::DragEvent& e) {
                    cube.yaw   += static_cast<float>(e.deltaX) * 0.012f;
                    cube.pitch += static_cast<float>(e.deltaY) * 0.012f;
                    cube.pitch  = std::clamp(cube.pitch, -1.55f, 1.55f);
                    everInteracted = true;
                })
                // Scroll wheel → zoom
                .onScroll([](const core::ScrollEvent& s) {
                    cube.zoom  *= 1.0f + static_cast<float>(s.y) * 0.10f;
                    cube.zoom   = std::clamp(cube.zoom, 0.25f, 5.0f);
                    everInteracted = true;
                })
                // Timer: drives continuous redraws (~60 fps) + auto-spin
                // onTimer fires during update() → needsRender_=true → render fires.
                // Incrementing cube.yaw here is immediately visible in that render.
                .onTimer(1.0f / 60.0f, [] {
                    if (!everInteracted) {
                        cube.yaw += 0.009f;   // ~0.54 rad/s auto-spin
                    }
                })
                .build();
#endif

            // Hint text
            components::text(ui, "hint")
                .position(0.0f, screen.height - 34.0f)
                .size(screen.width, 22.0f)
                .text("Left drag: rotate    Scroll: zoom")
                .fontSize(14.0f)
                .lineHeight(22.0f)
                .color({ 0.48f, 0.56f, 0.68f, 1.0f })
                .horizontalAlign(core::HorizontalAlign::Center)
                .build();

        }).build();
    }

} // namespace app
