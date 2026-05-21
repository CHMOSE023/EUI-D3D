/**
 * demo_win32_simple.cpp
 *
 * Win32 + Direct3D 11 — Enhanced Demo v2
 *
 *  - Menu bar:  File / Edit / View / Help，文字水平垂直居中
 *  - Toolbar:   图标按钮（Font Awesome），36×36，分组分隔
 *  - Tab bar:   多文档管理——左对齐固定宽度，带 × 关闭按钮，+ 新建
 *  - Tab 内容:  Documents / Images / Settings（可扩展）
 *  - 字体:      全局放大，易读性优先
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <imm.h>

#include "core/d3d_context.h"
#include "core/dsl_runtime.h"
#include "core/text.h"              // TextPrimitive::setDefaultFontFiles
#include "core/win32_input_bridge.h"
#include "core/async.h"
#include "components/button.h"
#include "components/contextmenu.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────
// 全局 EUI / Win32 状态
// ─────────────────────────────────────────────────────────────

static std::unique_ptr<core::Win32InputBridge>  g_bridge;
static std::unique_ptr<core::dsl::Runtime>      g_runtime;
static core::d3d::WindowSwapChain               g_swapChain;
static HWND                                     g_hwnd      = nullptr;
static bool                                     g_needsRender = true;

// ─────────────────────────────────────────────────────────────
// 文档 Tab 模型
// ─────────────────────────────────────────────────────────────

struct DocTab {
    std::string id;       // 稳定 ID，用于 EUI 元素 key 和 lambda 捕获
    std::string title;
    int         viewType; // 0=Document  1=Image  2=Settings
};

static std::vector<DocTab> g_tabs = {
    { "t1", "Document 1", 0 },
    { "t2", "image.png",  1 },
    { "t3", "Settings",   2 },
};
static int g_activeTabIdx = 0;
static int g_nextTabSeq   = 4;   // 下一个新建 tab 的序号

// ─────────────────────────────────────────────────────────────
// 应用状态
// ─────────────────────────────────────────────────────────────

static int         g_menuOpen   = -1;   // 哪个菜单项打开 (-1=关闭)
static int         g_clickCount = 0;
static std::string g_statusMsg  = "Ready";

// ─────────────────────────────────────────────────────────────
// 布局常量（逻辑像素）
// ─────────────────────────────────────────────────────────────

static constexpr float kMenuH    = 32.0f;
static constexpr float kToolbarH = 46.0f;
static constexpr float kTabsH    = 40.0f;
static constexpr float kHeaderH  = kMenuH + kToolbarH + kTabsH;  // 118
static constexpr float kStatusH  = 26.0f;

// ─────────────────────────────────────────────────────────────
// 菜单栏数据
// ─────────────────────────────────────────────────────────────

static constexpr int kMenuCount = 4;
static const char*   kMenuLabels[kMenuCount] = { "File", "Edit", "View", "Help" };
static const float   kMenuX[kMenuCount]      = { 0.0f, 60.0f, 120.0f, 180.0f };
static const float   kMenuW[kMenuCount]      = { 60.0f, 60.0f, 60.0f, 60.0f };

static const std::vector<std::string> kMenuItems[kMenuCount] = {
    { "New",    "Open",   "Save",     "Save As", "Exit"       },
    { "Undo",   "Redo",   "Cut",      "Copy",    "Paste",     "Select All" },
    { "Zoom In","Zoom Out","Reset Zoom","Full Screen"          },
    { "Documentation", "About"                                 },
};

// ─────────────────────────────────────────────────────────────
// 工具栏数据（图标用 Font Awesome 5/7 Free Solid 码点）
// ─────────────────────────────────────────────────────────────

struct ToolBtn {
    const char*  name;
    unsigned int icon;   // Font Awesome codepoint
    float        x;
    bool         sepAfter;
};

//  按钮 36×36，组内间距 6px，组间间距（sep）12px
//  Group1: New@8, Open@50, Save@92  → sep@132
//  Group2: Undo@142, Redo@184       → sep@224
//  Group3: Cut@234, Copy@276, Paste@318
static const ToolBtn kToolBtns[] = {
    { "New",   0xF15B, 8.0f,   false },
    { "Open",  0xF07C, 50.0f,  false },
    { "Save",  0xF0C7, 92.0f,  true  },
    { "Undo",  0xF0E2, 142.0f, false },
    { "Redo",  0xF01E, 184.0f, true  },
    { "Cut",   0xF0C4, 234.0f, false },
    { "Copy",  0xF0C5, 276.0f, false },
    { "Paste", 0xF0EA, 318.0f, false },
};
static constexpr int   kToolBtnCount = 8;
static constexpr float kToolBtnSz    = 36.0f;

// ─────────────────────────────────────────────────────────────
// 辅助：绝对定位文字（水平 Left，垂直 Center）
// ─────────────────────────────────────────────────────────────

static void label(core::dsl::Ui& ui, const std::string& id,
                  float x, float y, float w, float h,
                  const std::string& text, float fontSize,
                  const core::Color& color, int weight = 400) {
    ui.text(id)
        .x(x).y(y).size(w, h)
        .text(text).fontSize(fontSize).fontWeight(weight)
        .lineHeight(h).color(color)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

// ─────────────────────────────────────────────────────────────
// 1. 菜单栏（水平 + 垂直居中）
// ─────────────────────────────────────────────────────────────

static void buildMenuBar(core::dsl::Ui& ui, float W) {
    using core::Color;

    ui.rect("mb.bg").size(W, kMenuH)
        .color({ 0.13f, 0.13f, 0.15f, 1.0f }).build();
    ui.rect("mb.sep").y(kMenuH - 1.0f).size(W, 1.0f)
        .color({ 0.22f, 0.22f, 0.26f, 1.0f }).build();

    for (int i = 0; i < kMenuCount; ++i) {
        const bool isOpen    = (g_menuOpen == i);
        const Color activeBg = { 0.18f, 0.30f, 0.56f, 1.0f };
        const Color normalBg = isOpen ? activeBg : Color{ 0,0,0,0 };
        const Color hoverBg  = isOpen ? activeBg : Color{ 1,1,1,0.09f };

        // 可点击背景矩形
        ui.rect("mb.item." + std::to_string(i))
            .x(kMenuX[i]).y(2.0f).size(kMenuW[i], kMenuH - 4.0f)
            .states(normalBg, hoverBg, { 0.15f, 0.26f, 0.50f, 1.0f })
            .radius(4.0f)
            .onClick([i] {
                g_menuOpen = (g_menuOpen == i) ? -1 : i;
                g_needsRender = true;
            })
            .build();

        // 标签文字：水平 Center + 垂直 Center
        ui.text("mb.lbl." + std::to_string(i))
            .x(kMenuX[i]).y(0).size(kMenuW[i], kMenuH)
            .text(kMenuLabels[i])
            .fontSize(15.0f)
            .lineHeight(kMenuH)
            .color(isOpen ? Color{ 0.96f, 0.97f, 1.0f, 1.0f }
                          : Color{ 0.80f, 0.83f, 0.88f, 1.0f })
            .horizontalAlign(core::HorizontalAlign::Center)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
    }
}

// ─────────────────────────────────────────────────────────────
// 2. 工具栏（图标按钮，36×36）
// ─────────────────────────────────────────────────────────────

static void buildToolbar(core::dsl::Ui& ui, float W) {
    const float tbY   = kMenuH;
    const float btnY  = tbY + (kToolbarH - kToolBtnSz) * 0.5f;
    const float sepH  = kToolbarH - 20.0f;
    const float sepY  = tbY + 10.0f;

    ui.rect("tb.bg").y(tbY).size(W, kToolbarH)
        .color({ 0.12f, 0.12f, 0.14f, 1.0f }).build();
    ui.rect("tb.sep.bot").y(tbY + kToolbarH - 1.0f).size(W, 1.0f)
        .color({ 0.22f, 0.22f, 0.26f, 1.0f }).build();

    for (int i = 0; i < kToolBtnCount; ++i) {
        const ToolBtn& b = kToolBtns[i];
        std::string btnName = b.name;
        unsigned int ic     = b.icon;
        float bx = b.x;

        ui.stack("tb.s." + btnName)
            .x(bx).y(btnY).size(kToolBtnSz, kToolBtnSz)
            .content([&, btnName, ic] {
                components::button(ui, "tb.btn." + btnName)
                    .size(kToolBtnSz, kToolBtnSz)
                    .text("")            // 纯图标，无文字
                    .icon(ic)
                    .iconSize(16.0f)
                    .radius(7.0f)
                    .colors(
                        { 0.0f, 0.0f, 0.0f, 0.0f },
                        { 1.0f, 1.0f, 1.0f, 0.10f },
                        { 1.0f, 1.0f, 1.0f, 0.18f }
                    )
                    .iconColor({ 0.72f, 0.76f, 0.82f, 1.0f })
                    .transition(0.08f)
                    .onClick([btnName] {
                        g_statusMsg   = btnName;
                        g_needsRender = true;
                    })
                    .build();
            });

        // 组间竖线
        if (b.sepAfter) {
            const float sx = bx + kToolBtnSz + 4.0f;
            ui.rect("tb.vsep." + btnName)
                .x(sx).y(sepY).size(1.0f, sepH)
                .color({ 0.30f, 0.30f, 0.34f, 1.0f }).build();
        }
    }
}

// ─────────────────────────────────────────────────────────────
// 3. 多文档 Tab 栏（左对齐，固定宽，× 关闭，+ 新建）
// ─────────────────────────────────────────────────────────────

static void buildTabsBar(core::dsl::Ui& ui, float W) {
    using core::Color;

    static constexpr float tabW = 158.0f;   // 每个 tab 宽度
    static constexpr float tabH = kTabsH;
    const float tabY = kMenuH + kToolbarH;  // tab 条的顶部 y（在根坐标系）

    // 背景 + 底部线
    ui.rect("tabs.bg").y(tabY).size(W, tabH)
        .color({ 0.11f, 0.11f, 0.13f, 1.0f }).build();
    ui.rect("tabs.bot").y(tabY + tabH - 1.0f).size(W, 1.0f)
        .color({ 0.22f, 0.22f, 0.26f, 1.0f }).build();

    const int tabCount = static_cast<int>(g_tabs.size());

    for (int i = 0; i < tabCount; ++i) {
        const std::string tid  = g_tabs[i].id;
        const std::string titl = g_tabs[i].title;
        const float tx = static_cast<float>(i) * tabW;
        const bool  active = (i == g_activeTabIdx);

        // 背景：整个 tab 区域可点击（选中该 tab）
        const Color bgNorm  = active ? Color{ 0.17f, 0.17f, 0.20f, 1.0f }
                                     : Color{ 0.0f,  0.0f,  0.0f,  0.0f };
        const Color bgHov   = active ? bgNorm
                                     : Color{ 1.0f,  1.0f,  1.0f,  0.05f };

        ui.rect("tab.bg." + tid)
            .x(tx).y(tabY).size(tabW, tabH)
            .states(bgNorm, bgHov, bgNorm)
            .onClick([i, tid] {
                // 找到 tab（可能因关闭而 index 变动，按 id 定位）
                for (int j = 0; j < (int)g_tabs.size(); ++j) {
                    if (g_tabs[j].id == tid) { g_activeTabIdx = j; break; }
                }
                g_needsRender = true;
            })
            .build();

        // 活跃指示线（底部蓝色条）
        if (active) {
            ui.rect("tab.ind." + tid)
                .x(tx).y(tabY + tabH - 3.0f)
                .size(tabW, 3.0f)
                .color({ 0.22f, 0.46f, 0.90f, 1.0f })
                .build();
        }

        // 右侧分隔线（相邻 tab 之间）
        ui.rect("tab.div." + tid)
            .x(tx + tabW - 1.0f).y(tabY + 7.0f)
            .size(1.0f, tabH - 14.0f)
            .color({ 0.24f, 0.24f, 0.27f, 1.0f })
            .build();

        // 标题文字（留 28px 给关闭按钮）
        ui.text("tab.lbl." + tid)
            .x(tx + 12.0f).y(tabY).size(tabW - 40.0f, tabH)
            .text(titl).fontSize(14.0f)
            .lineHeight(tabH)
            .color(active ? Color{ 0.94f, 0.96f, 1.0f, 1.0f }
                          : Color{ 0.55f, 0.58f, 0.64f, 1.0f })
            .verticalAlign(core::VerticalAlign::Center)
            .build();

        // × 关闭按钮（覆盖 tab 右侧 28px；后渲染 = 优先命中）
        const float cx = tx + tabW - 28.0f;
        ui.rect("tab.cls." + tid)
            .x(cx).y(tabY + 6.0f).size(22.0f, tabH - 12.0f)
            .states({ 0,0,0,0 }, { 1,1,1,0.14f }, { 1,1,1,0.22f })
            .radius(4.0f)
            .onClick([tid] {
                auto it = std::find_if(g_tabs.begin(), g_tabs.end(),
                    [&tid](const DocTab& t) { return t.id == tid; });
                if (it != g_tabs.end() && g_tabs.size() > 1) {
                    int idx = (int)(it - g_tabs.begin());
                    g_tabs.erase(it);
                    if (g_activeTabIdx >= (int)g_tabs.size())
                        g_activeTabIdx = (int)g_tabs.size() - 1;
                    else if (g_activeTabIdx > idx)
                        --g_activeTabIdx;
                }
                g_needsRender = true;
            })
            .build();

        // × 号文字（覆盖在关闭按钮矩形上方，无 onClick → 点击穿透到下方矩形）
        ui.text("tab.x." + tid)
            .x(cx).y(tabY + 6.0f).size(22.0f, tabH - 12.0f)
            .text("×")
            .fontSize(14.0f)
            .lineHeight(tabH - 12.0f)
            .color(active ? Color{ 0.60f, 0.63f, 0.70f, 1.0f }
                          : Color{ 0.36f, 0.38f, 0.42f, 1.0f })
            .horizontalAlign(core::HorizontalAlign::Center)
            .verticalAlign(core::VerticalAlign::Center)
            .build();
    }

    // ── + 新建 Tab 按钮 ─────────────────────────────────────
    const float addX = static_cast<float>(tabCount) * tabW;

    ui.rect("tab.add.bg")
        .x(addX).y(tabY).size(40.0f, tabH)
        .states({ 0,0,0,0 }, { 1,1,1,0.07f }, { 1,1,1,0.13f })
        .onClick([] {
            int seq = g_nextTabSeq++;
            g_tabs.push_back({ "t" + std::to_string(seq),
                               "Document " + std::to_string(seq), 0 });
            g_activeTabIdx = (int)g_tabs.size() - 1;
            g_needsRender  = true;
        })
        .build();

    ui.text("tab.add.lbl")
        .x(addX).y(tabY).size(40.0f, tabH)
        .text("+")
        .fontSize(18.0f)
        .lineHeight(tabH)
        .color({ 0.48f, 0.50f, 0.56f, 1.0f })
        .horizontalAlign(core::HorizontalAlign::Center)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

// ─────────────────────────────────────────────────────────────
// Tab 内容：Documents
// ─────────────────────────────────────────────────────────────

static void buildDocumentsTab(core::dsl::Ui& ui, float W, float contentH) {
    using core::Color;
    const float M     = 28.0f;
    const float cardW = 440.0f;

    // 卡片1：点击计数
    const float c1y = 18.0f, c1h = 100.0f;
    ui.rect("doc.c1.bg").x(M).y(c1y).size(cardW, c1h)
        .color({ 0.14f, 0.14f, 0.16f, 1.0f }).radius(8.0f).build();
    label(ui, "doc.c1.hd", M + 16.0f, c1y + 10.0f, 240.0f, 24.0f,
          "Click Counter", 16.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, 600);

    ui.stack("doc.c1.btn").x(M + 16.0f).y(c1y + 48.0f).size(120.0f, 36.0f).content([&] {
        components::button(ui, "doc.btn.click")
            .size(120.0f, 36.0f).text("Click Me").fontSize(14.0f).radius(7.0f)
            .onClick([] { ++g_clickCount; g_needsRender = true; })
            .build();
    });
    label(ui, "doc.c1.cnt",
          M + 150.0f, c1y + 48.0f, 260.0f, 36.0f,
          "Count:  " + std::to_string(g_clickCount),
          15.0f, { 0.82f, 0.82f, 0.82f, 1.0f });

    // 卡片2：窗口信息
    const float c2y = 134.0f, c2h = 90.0f;
    ui.rect("doc.c2.bg").x(M).y(c2y).size(cardW, c2h)
        .color({ 0.14f, 0.14f, 0.16f, 1.0f }).radius(8.0f).build();
    label(ui, "doc.c2.hd", M + 16.0f, c2y + 8.0f, 240.0f, 24.0f,
          "Window Info", 16.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, 600);

    if (g_bridge) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Size:  %d × %d  px",
                      g_bridge->getWidth(), g_bridge->getHeight());
        label(ui, "doc.c2.sz",  M + 16.0f, c2y + 40.0f, 380.0f, 20.0f,
              buf, 13.5f, { 0.70f, 0.70f, 0.70f, 1.0f });
        std::snprintf(buf, sizeof(buf), "DPI Scale:  %.2f", g_bridge->getDpiScale());
        label(ui, "doc.c2.dpi", M + 16.0f, c2y + 64.0f, 380.0f, 20.0f,
              buf, 13.5f, { 0.70f, 0.70f, 0.70f, 1.0f });
    }

    // 卡片3：说明
    const float c3y = 240.0f, c3h = 148.0f;
    ui.rect("doc.c3.bg").x(M).y(c3y).size(cardW, c3h)
        .color({ 0.10f, 0.12f, 0.18f, 1.0f }).radius(8.0f).build();
    label(ui, "doc.c3.hd", M + 16.0f, c3y + 10.0f, 300.0f, 24.0f,
          "Usage Guide", 16.0f, { 0.50f, 0.74f, 1.0f, 1.0f }, 600);

    static const char* tips[] = {
        "Menu bar   ->  File / Edit / View / Help dropdowns",
        "Toolbar    ->  icon-only action buttons",
        "Tab bar    ->  left-aligned multi-document tabs",
        "  x  close a tab,   +  open a new document tab",
        "Images tab ->  local PNG + Bing Daily (async)",
    };
    for (int i = 0; i < 5; ++i) {
        label(ui, "doc.c3.tip." + std::to_string(i),
              M + 16.0f, c3y + 42.0f + i * 22.0f, 400.0f, 20.0f,
              tips[i], 13.0f, { 0.62f, 0.74f, 0.94f, 1.0f });
    }
}

// ─────────────────────────────────────────────────────────────
// Tab 内容：Images
// ─────────────────────────────────────────────────────────────

static void buildImagesTab(core::dsl::Ui& ui, float W, float contentH) {
    using core::Color;
    const float M     = 28.0f;
    const float cardY = 18.0f;

    // 左侧：应用图标
    const float iconCardW = 230.0f;
    const float imgSz     = iconCardW - 28.0f;
    const float iconCardH = imgSz + 60.0f;

    ui.rect("img.icon.card").x(M).y(cardY).size(iconCardW, iconCardH)
        .color({ 0.14f, 0.14f, 0.16f, 1.0f }).radius(12.0f).build();
    label(ui, "img.icon.hd", M + 14.0f, cardY + 10.0f, iconCardW - 28.0f, 22.0f,
          "App Icon", 14.5f, { 0.62f, 0.65f, 0.70f, 1.0f }, 500);

    ui.image("img.icon.img")
        .x(M + 14.0f).y(cardY + 38.0f)
        .size(imgSz, imgSz)
        .source("assets/icon.png")
        .contain()
        .radius(10.0f)
        .build();

    label(ui, "img.icon.sub",
          M + 14.0f, cardY + iconCardH - 26.0f, iconCardW - 28.0f, 20.0f,
          "icon.png (local)", 12.0f, { 0.42f, 0.44f, 0.48f, 1.0f });

    // 右侧：Bing Daily 壁纸
    const float bingX = M + iconCardW + 18.0f;
    const float bingW = W - bingX - M;
    const float bingH = iconCardH;

    ui.rect("img.bing.card").x(bingX).y(cardY).size(bingW, bingH)
        .color({ 0.14f, 0.14f, 0.16f, 1.0f }).radius(12.0f).build();
    label(ui, "img.bing.hd", bingX + 14.0f, cardY + 10.0f, bingW - 28.0f, 22.0f,
          "Bing Daily Wallpaper", 14.5f, { 0.62f, 0.65f, 0.70f, 1.0f }, 500);

    // 占位背景（加载前可见）
    ui.rect("img.bing.ph")
        .x(bingX + 10.0f).y(cardY + 38.0f)
        .size(bingW - 20.0f, bingH - 60.0f)
        .color({ 0.10f, 0.10f, 0.12f, 1.0f }).radius(8.0f).build();

    ui.image("img.bing.img")
        .x(bingX + 10.0f).y(cardY + 38.0f)
        .size(bingW - 20.0f, bingH - 60.0f)
        .bingDaily(0)
        .cover()
        .radius(8.0f)
        .build();

    label(ui, "img.bing.sub",
          bingX + 14.0f, cardY + bingH - 26.0f, bingW - 28.0f, 20.0f,
          "Fetched asynchronously from Bing", 12.0f, { 0.42f, 0.44f, 0.48f, 1.0f });

    label(ui, "img.note",
          M, cardY + bingH + 16.0f, W - M * 2.0f, 20.0f,
          "ui.image().source(\"path\")  for local   |   .bingDaily()  for network fetch",
          13.0f, { 0.36f, 0.40f, 0.46f, 1.0f });
}

// ─────────────────────────────────────────────────────────────
// Tab 内容：Settings
// ─────────────────────────────────────────────────────────────

static void buildSettingsTab(core::dsl::Ui& ui, float W, float contentH) {
    using core::Color;
    const float M = 28.0f;

    // 特性列表
    static const char* features[] = {
        "Pure Win32 window + message loop",
        "Direct3D 11 rendering (no OpenGL)",
        "Win32InputBridge  —  mouse & keyboard",
        "DPI-aware layout and SDF text",
        "Fluent Builder DSL (no XML / JSON)",
        "Font Awesome 7  —  icon font support",
    };
    static constexpr int featureCount = 6;
    const float rowH  = 28.0f;
    const float cardW = 460.0f;
    const float cardY = 18.0f;
    const float cardH = 50.0f + featureCount * rowH;

    ui.rect("set.feat.bg").x(M).y(cardY).size(cardW, cardH)
        .color({ 0.10f, 0.14f, 0.10f, 1.0f }).radius(10.0f).build();
    label(ui, "set.feat.hd", M + 16.0f, cardY + 10.0f, 300.0f, 26.0f,
          "Features", 17.0f, { 0.44f, 0.90f, 0.44f, 1.0f }, 600);

    for (int i = 0; i < featureCount; ++i) {
        label(ui, "set.feat." + std::to_string(i),
              M + 16.0f, cardY + 44.0f + i * rowH, cardW - 32.0f, 24.0f,
              std::string("+  ") + features[i],
              13.5f, { 0.68f, 0.86f, 0.68f, 1.0f });
    }

    // 主题色板
    const float palY = cardY + cardH + 18.0f;
    const float palH = 92.0f;
    ui.rect("set.pal.bg").x(M).y(palY).size(cardW, palH)
        .color({ 0.14f, 0.14f, 0.16f, 1.0f }).radius(10.0f).build();
    label(ui, "set.pal.hd", M + 16.0f, palY + 10.0f, 200.0f, 24.0f,
          "Theme Palette", 17.0f, { 0.84f, 0.84f, 0.90f, 1.0f }, 600);

    static const Color palette[] = {
        { 0.22f, 0.44f, 0.88f, 1.0f },
        { 0.26f, 0.76f, 0.52f, 1.0f },
        { 0.94f, 0.64f, 0.20f, 1.0f },
        { 0.90f, 0.28f, 0.28f, 1.0f },
        { 0.60f, 0.40f, 0.90f, 1.0f },
        { 0.38f, 0.78f, 0.96f, 1.0f },
    };
    const float swSz = 38.0f, swGap = 10.0f;
    for (int i = 0; i < 6; ++i) {
        ui.rect("set.sw." + std::to_string(i))
            .x(M + 16.0f + i * (swSz + swGap)).y(palY + 44.0f)
            .size(swSz, swSz).color(palette[i]).radius(9.0f).build();
    }
}

// ─────────────────────────────────────────────────────────────
// 状态栏
// ─────────────────────────────────────────────────────────────

static void buildStatusBar(core::dsl::Ui& ui, float W, float H) {
    const float y = H - kStatusH;
    ui.rect("sts.bg").y(y).size(W, kStatusH)
        .color({ 0.11f, 0.11f, 0.12f, 1.0f }).build();
    ui.rect("sts.line").y(y).size(W, 1.0f)
        .color({ 0.22f, 0.22f, 0.26f, 1.0f }).build();
    label(ui, "sts.txt", 14.0f, y, W - 28.0f, kStatusH,
          g_statusMsg, 13.5f, { 0.52f, 0.55f, 0.60f, 1.0f });
}

// ─────────────────────────────────────────────────────────────
// 菜单栏上下文菜单（最后渲染）
// ─────────────────────────────────────────────────────────────

static void buildContextMenus(core::dsl::Ui& ui, float W, float H) {
    for (int i = 0; i < kMenuCount; ++i) {
        components::contextMenu(ui, "ctx.menu." + std::to_string(i))
            .screen(W, H)
            .position(kMenuX[i], kMenuH)
            .size(200.0f, 34.0f)
            .items(kMenuItems[i])
            .open(g_menuOpen == i)
            .onSelect([i](int idx) {
                if (i == 0 && idx == 4) {
                    PostQuitMessage(0);
                } else {
                    g_statusMsg = std::string(kMenuLabels[i]) +
                                  " > " + kMenuItems[i][idx];
                }
                g_menuOpen  = -1;
                g_needsRender = true;
            })
            .onDismiss([] {
                g_menuOpen  = -1;
                g_needsRender = true;
            })
            .build();
    }
}

// ─────────────────────────────────────────────────────────────
// 主合成函数
// ─────────────────────────────────────────────────────────────

static void composeUI(core::dsl::Ui& ui, const core::dsl::Screen& screen) {
    const float W        = screen.width;
    const float H        = screen.height;
    const float contentH = H - kHeaderH - kStatusH;

    ui.stack("root").size(W, H).content([&] {
        // 全窗口底色
        ui.rect("bg").size(W, H)
            .color({ 0.09f, 0.09f, 0.10f, 1.0f }).build();

        // 三层头部
        buildMenuBar(ui, W);
        buildToolbar(ui, W);
        buildTabsBar(ui, W);

        // 内容区（根据当前活跃 tab 的 viewType 决定显示哪一页）
        ui.stack("content").x(0).y(kHeaderH)
            .size(W, contentH > 0.0f ? contentH : 0.0f).content([&] {
            if (!g_tabs.empty()) {
                const int vt = g_tabs[g_activeTabIdx].viewType;
                if      (vt == 0) buildDocumentsTab(ui, W, contentH);
                else if (vt == 1) buildImagesTab(ui, W, contentH);
                else              buildSettingsTab(ui, W, contentH);
            }
        });

        // 状态栏
        buildStatusBar(ui, W, H);

        // 下拉菜单（最顶层）
        buildContextMenus(ui, W, H);
    });
}

// ─────────────────────────────────────────────────────────────
// 渲染一帧
// ─────────────────────────────────────────────────────────────

static std::chrono::high_resolution_clock::time_point g_lastFrame;

static void renderFrame(bool force) {
    if (!g_runtime || !g_bridge || !g_swapChain.swapChain) return;

    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    const int fbW = rc.right  - rc.left;
    const int fbH = rc.bottom - rc.top;
    if (fbW <= 0 || fbH <= 0) return;

    if (fbW != g_swapChain.width || fbH != g_swapChain.height) {
        core::d3d::resizeSwapChain(g_swapChain, fbW, fbH);
        g_bridge->handleSizeChanged(fbW, fbH);
        g_needsRender = true;
    }

    const auto  now   = std::chrono::high_resolution_clock::now();
    const float delta = std::chrono::duration<float>(now - g_lastFrame).count();
    g_lastFrame = now;

    const float dpi      = g_bridge->getDpiScale();
    const float ptrScale = g_bridge->getPointerScale();

    if (g_runtime->update(g_bridge->getWindowHandle(), delta, ptrScale, dpi))
        g_needsRender = true;
    if (core::async::dispatchReady())
        g_needsRender = true;

    if (g_needsRender || force) {
        const float logW = static_cast<float>(fbW) / dpi;
        const float logH = static_cast<float>(fbH) / dpi;
        g_runtime->compose("main", logW, logH,
            [](core::dsl::Ui& ui, const core::dsl::Screen& scr) {
                composeUI(ui, scr);
            });
        g_runtime->render(fbW, fbH, dpi, { 0.09f, 0.09f, 0.10f, 1.0f }, &g_swapChain);
        g_swapChain.swapChain->Present(1, 0);
        g_needsRender = false;
    }
}

// ─────────────────────────────────────────────────────────────
// Win32 窗口过程
// ─────────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        if (g_bridge) g_bridge->processMessage(msg, wParam, lParam);
        g_needsRender = true;
        if (wParam != SIZE_MINIMIZED) renderFrame(true);
        return 0;

    default:
        if (g_bridge) {
            g_bridge->processMessage(msg, wParam, lParam);
            switch (msg) {
            case WM_KEYDOWN: case WM_CHAR:
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN: case WM_LBUTTONUP:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP:
            case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
            case WM_DPICHANGED:
                g_needsRender = true;
                break;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ─────────────────────────────────────────────────────────────
// 主函数
// ─────────────────────────────────────────────────────────────

int main() {
    SetProcessDPIAware();
    timeBeginPeriod(1);

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"EUIDemoWin32Class";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.style         = CS_VREDRAW | CS_HREDRAW;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        L"EUIDemoWin32Class",
        L"EUI-D3D  Win32 Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1020, 700,
        nullptr, nullptr,
        hInst, nullptr
    );
    if (!hwnd) {
        MessageBoxW(nullptr, L"CreateWindow failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // D3D11 设备 + 交换链
    if (!core::d3d::createDevice()) {
        MessageBoxW(hwnd, L"D3D11 device creation failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    const int fbW = clientRect.right  - clientRect.left;
    const int fbH = clientRect.bottom - clientRect.top;
    g_swapChain = core::d3d::createSwapChain(hwnd, fbW, fbH);
    core::d3d::setCurrentSwapChain(&g_swapChain);

    // 输入桥接器（必须先于 Runtime）
    g_bridge = std::make_unique<core::Win32InputBridge>(hwnd);

    // 字体：加载 Font Awesome 图标字体（供工具栏使用）
    core::TextPrimitive::setDefaultFontFiles(
        "",  // 文字字体：使用内置默认字体
        "assets/Font Awesome 7 Free-Solid-900.otf"  // 图标字体
    );

    // EUI 运行时
    g_runtime = std::make_unique<core::dsl::Runtime>();
    g_runtime->initializeWin32(hwnd, &g_swapChain, g_bridge->getWindowHandle());

    // 主消息循环
    MSG msg{};
    bool running    = true;
    g_lastFrame     = std::chrono::high_resolution_clock::now();
    int  frameCount = 0;
    auto lastFpsTp  = g_lastFrame;

    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        RECT cr{};
        GetClientRect(hwnd, &cr);
        if ((cr.right - cr.left) <= 0 || (cr.bottom - cr.top) <= 0) {
            WaitMessage();
            continue;
        }

        const bool willRender = g_needsRender;
        renderFrame(false);
        if (willRender) ++frameCount;

        if (!g_needsRender) WaitMessage();

        const auto  now2  = std::chrono::high_resolution_clock::now();
        const float fpsDt = std::chrono::duration<float>(now2 - lastFpsTp).count();
        if (fpsDt >= 1.0f) {
            const float fps = static_cast<float>(frameCount) / fpsDt;
            wchar_t title[160];
            swprintf_s(title,
                L"EUI-D3D Win32 Demo  |  Menu + Toolbar + Tabs + Images  |  %.0f FPS", fps);
            SetWindowTextW(hwnd, title);
            frameCount = 0;
            lastFpsTp  = now2;
        }
    }

    g_runtime->shutdown(false);
    g_runtime.reset();
    g_bridge.reset();
    g_swapChain.rtv.Reset();
    g_swapChain.swapChain.Reset();
    UnregisterClassW(L"EUIDemoWin32Class", hInst);
    timeEndPeriod(1);
    return 0;
}
