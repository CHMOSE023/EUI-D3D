#pragma once

#include <string>

struct GLFWwindow;

namespace core::platform {

    struct TrayOptions {
        std::string tooltip;
        std::string iconPath;
    };

    bool openUrl(const std::string& url);
    bool initializeTray(const TrayOptions& options);
    bool isTrayInitialized();
    void pollTray(bool blocking = false);
    bool consumeTrayShowRequested();
    bool consumeTrayExitRequested();
    void shutdownTray();
    void setImeCursorRect(GLFWwindow* window, float x, float y, float width, float height);

} // namespace core::platform
