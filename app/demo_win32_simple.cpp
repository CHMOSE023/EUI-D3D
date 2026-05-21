/**
 * demo_win32_simple.cpp
 *
 * 纯 Win32 + Direct3D 11 Demo
 * - 不使用 GLFW（窗口/消息循环完全由 Win32 API 管理）
 * - 使用 Win32InputBridge 桥接输入事件到 EUI
 * - 使用 Runtime::initializeWin32 避免 GLFW 回调污染
 *
 * 适合作为在 MiniCAD 等现有 Win32 项目中集成 EUI-D3D 的参考
 *
 * 编译（D3D11 模式）：
 *   cmake -B build-d3d11 -DEUI_D3D11=ON
 *   cmake --build build-d3d11 --target demo_win32_simple
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
#include "core/win32_input_bridge.h"
#include "core/async.h"
#include "components/button.h"

#include <chrono>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────
// 全局状态
// ─────────────────────────────────────────────────────────────

static std::unique_ptr<core::Win32InputBridge>  g_bridge;
static std::unique_ptr<core::dsl::Runtime>      g_runtime;
static core::d3d::WindowSwapChain               g_swapChain;
static HWND                                     g_hwnd = nullptr;
static bool                                     g_needsRender = true;

// Demo 应用状态
static int         g_clickCount = 0;
static std::string g_inputText;

// ─────────────────────────────────────────────────────────────
// 应用 UI（compose 函数）
// ─────────────────────────────────────────────────────────────

// 文字辅助：绝对定位 + 显式尺寸 + 垂直居中（与 clock.cpp 一致的可靠模式）
static void label(core::dsl::Ui& ui, const std::string& id,
                  float x, float y, float w, float h,
                  const std::string& text, float fontSize,
                  const core::Color& color, int weight = 400) {
    ui.text(id)
        .x(x).y(y).size(w, h)
        .text(text)
        .fontSize(fontSize)
        .fontWeight(weight)
        .lineHeight(h)
        .color(color)
        .verticalAlign(core::VerticalAlign::Center)
        .build();
}

static void composeUI(core::dsl::Ui& ui, const core::dsl::Screen& screen) {
    using core::Color;

    // 根：全窗口 stack + 绝对定位子元素（本代码库的成熟布局模式）
    ui.stack("root").size(screen.width, screen.height).content([&] {
        const float M = 30.0f;        // 外边距
        const float cardW = 460.0f;

        // ── 标题区 ──────────────────────────────────────────────
        label(ui, "title", M, 24.0f, 600.0f, 40.0f,
              "EUI-D3D  Win32 Demo", 28.0f, {0.95f, 0.96f, 0.98f, 1.0f}, 700);
        label(ui, "subtitle", M, 68.0f, 600.0f, 22.0f,
              "Pure Win32 + Direct3D 11  |  No GLFW", 14.0f, {0.6f, 0.7f, 0.8f, 1.0f});

        ui.rect("divider").x(M).y(100.0f).size(cardW, 1.0f)
            .color({0.25f, 0.25f, 0.25f, 1.0f}).build();

        // ── 卡片 1：点击计数 ────────────────────────────────────
        const float c1y = 120.0f, c1h = 96.0f;
        ui.rect("card.click.bg").x(M).y(c1y).size(cardW, c1h)
            .color({0.14f, 0.14f, 0.16f, 1.0f}).radius(8.0f).build();

        label(ui, "card.click.title", M + 16.0f, c1y + 12.0f, 300.0f, 20.0f,
              "Click Counter", 15.0f, {1.0f, 1.0f, 1.0f, 1.0f}, 600);

        // 按钮（用组件）：放进一个绝对定位的 holder stack
        ui.stack("btn.holder").x(M + 16.0f).y(c1y + 44.0f).size(120.0f, 36.0f).content([&] {
            components::button(ui, "btn_click")
                .size(120.0f, 36.0f)
                .text("Click Me")
                .fontSize(13.0f)
                .radius(6.0f)
                .onClick([&] { ++g_clickCount; g_needsRender = true; })
                .build();
        });

        label(ui, "click.count", M + 152.0f, c1y + 44.0f, 280.0f, 36.0f,
              "Count: " + std::to_string(g_clickCount), 14.0f, {0.85f, 0.85f, 0.85f, 1.0f});

        // ── 卡片 2：窗口信息 ────────────────────────────────────
        const float c2y = 230.0f, c2h = 86.0f;
        ui.rect("card.info.bg").x(M).y(c2y).size(cardW, c2h)
            .color({0.14f, 0.14f, 0.16f, 1.0f}).radius(8.0f).build();

        label(ui, "card.info.title", M + 16.0f, c2y + 10.0f, 300.0f, 20.0f,
              "Window Info", 15.0f, {1.0f, 1.0f, 1.0f, 1.0f}, 600);

        if (g_bridge) {
            label(ui, "info.size", M + 16.0f, c2y + 36.0f, 420.0f, 18.0f,
                  "Size: " + std::to_string(g_bridge->getWidth()) + " x " +
                  std::to_string(g_bridge->getHeight()) + " px",
                  12.0f, {0.75f, 0.75f, 0.75f, 1.0f});

            char dpiStr[64];
            std::snprintf(dpiStr, sizeof(dpiStr), "DPI Scale: %.2f", g_bridge->getDpiScale());
            label(ui, "info.dpi", M + 16.0f, c2y + 58.0f, 420.0f, 18.0f,
                  dpiStr, 12.0f, {0.75f, 0.75f, 0.75f, 1.0f});
        }

        // ── 卡片 3：特性列表 ────────────────────────────────────
        const float c3y = 330.0f, c3h = 168.0f;
        ui.rect("card.feat.bg").x(M).y(c3y).size(cardW, c3h)
            .color({0.10f, 0.14f, 0.10f, 1.0f}).radius(8.0f).build();

        label(ui, "feat.title", M + 16.0f, c3y + 12.0f, 300.0f, 20.0f,
              "Features", 15.0f, {0.5f, 0.9f, 0.5f, 1.0f}, 600);

        const char* features[] = {
            "Pure Win32 window + message loop",
            "Direct3D 11 rendering (no OpenGL)",
            "Win32InputBridge for input events",
            "DPI-aware layout and fonts",
            "No GLFW dependency at runtime",
        };
        int fi = 0;
        for (const char* f : features) {
            label(ui, "feat." + std::to_string(fi),
                  M + 16.0f, c3y + 42.0f + static_cast<float>(fi) * 24.0f, 420.0f, 20.0f,
                  std::string("+  ") + f, 12.0f, {0.8f, 0.9f, 0.8f, 1.0f});
            ++fi;
        }
    });
}

// ─────────────────────────────────────────────────────────────
// 渲染一帧（可从主循环或 WM_SIZE 模态循环中调用）
// ─────────────────────────────────────────────────────────────

static std::chrono::high_resolution_clock::time_point g_lastFrame;

static void renderFrame(bool force) {
    if (!g_runtime || !g_bridge || !g_swapChain.swapChain) {
        return;
    }

    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    int fbW = rc.right - rc.left;
    int fbH = rc.bottom - rc.top;
    if (fbW <= 0 || fbH <= 0) {
        return;  // 最小化
    }

    // 交换链 resize
    if (fbW != g_swapChain.width || fbH != g_swapChain.height) {
        core::d3d::resizeSwapChain(g_swapChain, fbW, fbH);
        g_bridge->handleSizeChanged(fbW, fbH);
        g_needsRender = true;
    }

    auto  now   = std::chrono::high_resolution_clock::now();
    float delta = std::chrono::duration<float>(now - g_lastFrame).count();
    g_lastFrame = now;

    const float dpi      = g_bridge->getDpiScale();
    const float ptrScale = g_bridge->getPointerScale();

    if (g_runtime->update(g_bridge->getWindowHandle(), delta, ptrScale, dpi)) {
        g_needsRender = true;
    }
    if (core::async::dispatchReady()) {
        g_needsRender = true;
    }

    if (g_needsRender || force) {
        const float logW = static_cast<float>(fbW) / dpi;
        const float logH = static_cast<float>(fbH) / dpi;
        g_runtime->compose("main", logW, logH, [](core::dsl::Ui& ui, const core::dsl::Screen& scr) {
            composeUI(ui, scr);
        });
        g_runtime->render(fbW, fbH, dpi, {0.09f, 0.09f, 0.10f, 1.0f}, &g_swapChain);
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
        return 1;  // 防止背景闪烁（D3D11 自己清屏）

    case WM_SIZE:
        // 关键：拖拽改变大小时进入模态循环，主循环拿不到控制权。
        // 在此直接 resize + 重绘，保证实时跟随。
        if (g_bridge) {
            g_bridge->processMessage(msg, wParam, lParam);
        }
        g_needsRender = true;
        if (wParam != SIZE_MINIMIZED) {
            renderFrame(true);
        }
        return 0;

    default:
        // 所有输入消息委托给 Win32InputBridge
        if (g_bridge) {
            g_bridge->processMessage(msg, wParam, lParam);
            // 输入消息触发重绘
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

    // ── 注册窗口类 ────────────────────────────────────────────
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"EUIDemoWin32Class";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.style         = CS_VREDRAW | CS_HREDRAW;
    RegisterClassW(&wc);

    // ── 创建窗口 ──────────────────────────────────────────────
    constexpr int kWidth  = 900;
    constexpr int kHeight = 640;

    HWND hwnd = CreateWindowExW(
        0,
        L"EUIDemoWin32Class",
        L"EUI-D3D Win32 Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        kWidth, kHeight,
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

    // ── 初始化 D3D11 ─────────────────────────────────────────
    if (!core::d3d::createDevice()) {
        MessageBoxW(hwnd, L"D3D11 device creation failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 初始交换链尺寸用客户区大小
    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    int fbW = clientRect.right  - clientRect.left;
    int fbH = clientRect.bottom - clientRect.top;

    g_swapChain = core::d3d::createSwapChain(hwnd, fbW, fbH);
    core::d3d::setCurrentSwapChain(&g_swapChain);

    // ── 初始化输入桥接器 ──────────────────────────────────────
    // 必须先于 Runtime 初始化，因为它会设置 pointerState.win32Mode
    g_bridge = std::make_unique<core::Win32InputBridge>(hwnd);

    // ── 初始化 EUI 运行时 ─────────────────────────────────────
    g_runtime = std::make_unique<core::dsl::Runtime>();
    // 使用 initializeWin32：跳过 GLFW 回调安装，设置 Win32 光标/IME 处理
    g_runtime->initializeWin32(hwnd, &g_swapChain, g_bridge->getWindowHandle());

    // ── 主循环 ────────────────────────────────────────────────
    MSG msg{};
    bool running = true;
    g_lastFrame      = std::chrono::high_resolution_clock::now();
    int  frameCount   = 0;
    auto lastFpsStamp = g_lastFrame;

    while (running) {
        // 处理 Windows 消息
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        // 最小化时等待消息，省 CPU
        GetClientRect(hwnd, &clientRect);
        if ((clientRect.right - clientRect.left) <= 0 ||
            (clientRect.bottom - clientRect.top) <= 0) {
            WaitMessage();
            continue;
        }

        const bool didRender = g_needsRender;
        renderFrame(false);
        if (didRender) {
            ++frameCount;
        }

        // 无重绘需求时等待消息，节省 CPU
        if (!g_needsRender) {
            WaitMessage();
        }

        // 每秒更新标题栏 FPS
        auto now = std::chrono::high_resolution_clock::now();
        auto fpsDur = std::chrono::duration<float>(now - lastFpsStamp).count();
        if (fpsDur >= 1.0f) {
            float fps = static_cast<float>(frameCount) / fpsDur;
            wchar_t title[128];
            swprintf_s(title, L"EUI-D3D Win32 Demo  |  %.0f FPS", fps);
            SetWindowTextW(hwnd, title);
            frameCount  = 0;
            lastFpsStamp = now;
        }
    }

    // ── 清理 ──────────────────────────────────────────────────
    g_runtime->shutdown(false);
    g_runtime.reset();
    g_bridge.reset();

    g_swapChain.rtv.Reset();
    g_swapChain.swapChain.Reset();

    UnregisterClassW(L"EUIDemoWin32Class", hInst);
    timeEndPeriod(1);
    return 0;
}
