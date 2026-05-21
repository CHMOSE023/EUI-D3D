/**
 * Win32 Input Bridge for EUI-D3D
 *
 * 提供 Win32 消息处理与 EUI 输入系统的桥接
 * 在纯 Win32 + D3D11 环境中使用，无需 GLFW
 *
 * 使用示例：
 *   Win32InputBridge bridge(hwnd);  // 创建桥接器，传入 HWND
 *
 *   // 在窗口消息处理中
 *   case WM_KEYDOWN:
 *       bridge.handleKeyDown(wParam);
 *       break;
 *   case WM_CHAR:
 *       bridge.handleChar(wParam);
 *       break;
 *   // ... 其他消息
 *
 *   // 在主循环的 EUI 更新前
 *   float pointerScale = bridge.getPointerScale();
 *   GLFWwindow* windowHandle = bridge.getWindowHandle();
 */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "core/event.h"
#include <memory>

namespace core {

/**
 * Win32 输入事件桥接器
 *
 * 负责将 Win32 窗口消息转换为 EUI 内部的输入队列事件
 */
class Win32InputBridge {
public:
    Win32InputBridge(HWND hwnd)
        : hwnd_(hwnd), windowHandle_((void*)hwnd), dpiScale_(1.0f), pointerScale_(1.0f) {
        updateDpiScale();
        // 初始化 EUI 的输入队列（使用 hwnd 作为 key）
        detail::inputQueue((GLFWwindow*)windowHandle_);
        // 启用 Win32 模式：告知 readPointerEvent 不调用 GLFW API
        detail::PointerState& ps = detail::pointerState((GLFWwindow*)windowHandle_);
        ps.win32Mode = true;
    }

    // ─────────────────────────────────────────────────────────
    // 窗口句柄和缩放因子
    // ─────────────────────────────────────────────────────────

    /**
     * 获取用于 EUI 的 GLFWwindow* 句柄（实际上是 void* 指针）
     * 在调用 EUI 的 initialize、update 等函数时传入此值
     */
    GLFWwindow* getWindowHandle() const {
        return (GLFWwindow*)windowHandle_;
    }

    /**
     * 获取 DPI 缩放因子（用于 UI 布局）
     * 例如：150% DPI = 1.5，200% DPI = 2.0
     */
    float getDpiScale() const {
        return dpiScale_;
    }

    /**
     * 获取指针缩放因子（用于鼠标坐标）
     * 通常与 DPI 缩放相同
     */
    float getPointerScale() const {
        return pointerScale_;
    }

    // ─────────────────────────────────────────────────────────
    // 键盘事件处理
    // ─────────────────────────────────────────────────────────

    /**
     * 处理 WM_KEYDOWN 消息
     */
    void handleKeyDown(WPARAM vkey) {
        auto& queue = detail::inputQueue((GLFWwindow*)windowHandle_);

        switch (vkey) {
        case VK_BACK:
            queue.backspace = true;
            break;
        case VK_DELETE:
            queue.del = true;
            break;
        case VK_RETURN:
            queue.enter = true;
            break;
        case VK_LEFT:
            queue.left = true;
            break;
        case VK_RIGHT:
            queue.right = true;
            break;
        case VK_UP:
            // 可扩展：上键处理
            break;
        case VK_DOWN:
            // 可扩展：下键处理
            break;
        case VK_HOME:
            queue.home = true;
            break;
        case VK_END:
            queue.end = true;
            break;
        case VK_ESCAPE:
            queue.escape = true;
            break;
        case 'A':
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                queue.selectAll = true;
            }
            break;
        case 'C':
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                queue.copy = true;
            }
            break;
        case 'X':
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                queue.cut = true;
            }
            break;
        case 'V':
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // 粘贴处理（需要从剪贴板读取）
                handlePaste();
            }
            break;
        }
    }

    /**
     * 处理 WM_KEYUP 消息（可选）
     */
    void handleKeyUp(WPARAM vkey) {
        // 目前 EUI 不需要 key release 事件，但可以在此预留
    }

    /**
     * 处理 WM_CHAR 消息（文字输入）
     * wParam 是 Unicode 字符代码点
     */
    void handleChar(wchar_t ch) {
        auto& queue = detail::inputQueue((GLFWwindow*)windowHandle_);

        // 排除控制字符
        if (ch < 0x20 || ch == 127) {
            return;
        }

        // UTF-16 到 UTF-8 转换
        if (ch <= 0x7F) {
            // ASCII
            queue.text.push_back(static_cast<char>(ch));
        } else if (ch <= 0x7FF) {
            // 2-byte UTF-8
            queue.text.push_back(static_cast<char>(0xC0 | ((ch >> 6) & 0x1F)));
            queue.text.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            // 3-byte UTF-8
            queue.text.push_back(static_cast<char>(0xE0 | ((ch >> 12) & 0x0F)));
            queue.text.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            queue.text.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }

    // ─────────────────────────────────────────────────────────
    // 鼠标事件处理
    // ─────────────────────────────────────────────────────────

    /**
     * 处理 WM_MOUSEMOVE 消息
     * 参数：x, y 是客户端坐标（物理像素）
     */
    void handleMouseMove(int x, int y) {
        auto& state = detail::pointerState((GLFWwindow*)windowHandle_);
        // 写入 win32 当前帧坐标（readPointerEvent 会在每帧消费并更新 lastX/Y）
        state.win32X = static_cast<double>(x);
        state.win32Y = static_cast<double>(y);
    }

    /**
     * 处理 WM_LBUTTONDOWN 消息
     */
    void handleMouseLeftDown(int x, int y) {
        auto& state = detail::pointerState((GLFWwindow*)windowHandle_);
        state.win32X = static_cast<double>(x);
        state.win32Y = static_cast<double>(y);
        state.win32Down = true;

        if (hwnd_) {
            SetCapture(hwnd_);  // 捕获鼠标即使移出窗口
        }
    }

    /**
     * 处理 WM_LBUTTONUP 消息
     */
    void handleMouseLeftUp(int x, int y) {
        auto& state = detail::pointerState((GLFWwindow*)windowHandle_);
        state.win32X = static_cast<double>(x);
        state.win32Y = static_cast<double>(y);
        state.win32Down = false;

        ReleaseCapture();
    }

    /**
     * 处理 WM_RBUTTONDOWN 消息
     */
    void handleMouseRightDown(int x, int y) {
        auto& state = detail::pointerState((GLFWwindow*)windowHandle_);
        state.win32X = static_cast<double>(x);
        state.win32Y = static_cast<double>(y);
        state.win32RightDown = true;

        if (hwnd_) {
            SetCapture(hwnd_);
        }
    }

    /**
     * 处理 WM_RBUTTONUP 消息
     */
    void handleMouseRightUp(int x, int y) {
        auto& state = detail::pointerState((GLFWwindow*)windowHandle_);
        state.win32X = static_cast<double>(x);
        state.win32Y = static_cast<double>(y);
        state.win32RightDown = false;

        ReleaseCapture();
    }

    /**
     * 处理 WM_MOUSEWHEEL 消息
     * delta：来自 GET_WHEEL_DELTA_WPARAM(wParam)
     *        正值表示向上滚动，负值表示向下
     */
    void handleMouseWheel(int delta) {
        auto& queue = detail::inputQueue((GLFWwindow*)windowHandle_);
        // WHEEL_DELTA = 120
        queue.scrollY += static_cast<double>(delta) / 120.0;
    }

    /**
     * 处理 WM_MOUSEHWHEEL 消息（水平滚轮）
     */
    void handleMouseHWheel(int delta) {
        auto& queue = detail::inputQueue((GLFWwindow*)windowHandle_);
        queue.scrollX += static_cast<double>(delta) / 120.0;
    }

    // ─────────────────────────────────────────────────────────
    // 窗口事件处理
    // ─────────────────────────────────────────────────────────

    /**
     * 处理 WM_SIZE 或 WM_DPICHANGED 消息
     * 需要在 dsl_runtime 中重新计算布局
     */
    void handleSizeChanged(int newWidth, int newHeight) {
        width_ = newWidth;
        height_ = newHeight;
        // EUI 会在下一帧自动重新布局
    }

    /**
     * 处理 DPI 变化（WM_DPICHANGED）
     * 在 Windows 10/11 中，当窗口从高 DPI 移到低 DPI 屏幕时触发
     */
    void handleDpiChanged() {
        updateDpiScale();
    }

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // ─────────────────────────────────────────────────────────
    // 完整消息处理示例
    // ─────────────────────────────────────────────────────────

    /**
     * 在 WndProc 中调用此函数处理所有消息
     * 返回：是否已处理此消息（需要继续默认处理时返回 false）
     */
    bool processMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_KEYDOWN:
            handleKeyDown(wParam);
            return false;  // 继续默认处理

        case WM_KEYUP:
            handleKeyUp(wParam);
            return false;

        case WM_CHAR:
            handleChar(static_cast<wchar_t>(wParam));
            return 0;

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            handleMouseMove(x, y);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            handleMouseLeftDown(x, y);
            return 0;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            handleMouseLeftUp(x, y);
            return 0;
        }

        case WM_RBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            handleMouseRightDown(x, y);
            return 0;
        }

        case WM_RBUTTONUP: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            handleMouseRightUp(x, y);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            handleMouseWheel(delta);
            return 0;
        }

        case WM_MOUSEHWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            handleMouseHWheel(delta);
            return 0;
        }

        case WM_SIZE: {
            int w = GET_X_LPARAM(lParam);
            int h = GET_Y_LPARAM(lParam);
            handleSizeChanged(w, h);
            return 0;
        }

        case WM_DPICHANGED:
            handleDpiChanged();
            return 0;

        default:
            return false;
        }
    }

private:
    HWND hwnd_;
    void* windowHandle_;
    int width_ = 1280;
    int height_ = 720;
    float dpiScale_;
    float pointerScale_;

    void updateDpiScale() {
        if (hwnd_) {
            // 方法 1: GetDpiForWindow（需要 Windows 10 1607+）
            // unsigned int dpi = GetDpiForWindow(hwnd_);
            // dpiScale_ = static_cast<float>(dpi) / 96.0f;

            // 方法 2: 通过 DC 获取 DPI（兼容性更好）
            HDC hdc = GetDC(hwnd_);
            if (hdc) {
                int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
                dpiScale_ = static_cast<float>(dpiX) / 96.0f;
                ReleaseDC(hwnd_, hdc);
            } else {
                dpiScale_ = 1.0f;
            }

            pointerScale_ = dpiScale_;
        }
    }

    void handlePaste() {
        // 从 Windows 剪贴板读取文本
        if (!OpenClipboard(hwnd_)) {
            return;
        }

        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
            if (pszText) {
                auto& queue = detail::inputQueue((GLFWwindow*)windowHandle_);

                // UTF-16 到 UTF-8
                for (const wchar_t* p = pszText; *p; ++p) {
                    handleChar(*p);  // 复用字符转换逻辑
                }

                GlobalUnlock(hData);
            }
        }

        CloseClipboard();
    }
};

}  // namespace core
