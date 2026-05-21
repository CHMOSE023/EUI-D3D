#include "core/platform.h"

#include "core/ime_bridge.h"
#include "core/tray_bridge.h"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace core::platform {

    namespace {

#if !defined(_WIN32)
        std::string shellQuote(const std::string& value) {
            std::string result = "'";
            for (char ch : value) {
                if (ch == '\'') {
                    result += "'\\''";
                }
                else {
                    result.push_back(ch);
                }
            }
            result += "'";
            return result;
        }
#endif

        struct TrayState {
            bool initialized = false;
            std::string iconPath;
        };

        TrayState& trayState() {
            static TrayState state;
            return state;
        }

        std::filesystem::path resolveIconPath(const std::string& iconPath) {
            namespace fs = std::filesystem;
            if (iconPath.empty()) {
                return {};
            }

            std::error_code error;
            const fs::path requested(iconPath);
            const fs::path current = fs::current_path(error);
            std::vector<fs::path> candidates;
            candidates.push_back(requested);
            if (!error) {
                candidates.push_back(current / requested);
                candidates.push_back(current / "assets" / requested.filename());
            }

#if defined(_WIN32)
            const std::size_t originalCount = candidates.size();
            std::vector<fs::path> icoCandidates;
            for (std::size_t i = 0; i < originalCount; ++i) {
                fs::path ico = candidates[i];
                if (ico.extension() != ".ico") {
                    ico.replace_extension(".ico");
                }
                icoCandidates.push_back(std::move(ico));
            }
            icoCandidates.insert(icoCandidates.end(), candidates.begin(), candidates.end());
            candidates = std::move(icoCandidates);
#endif

            for (const fs::path& candidate : candidates) {
                error.clear();
                if (fs::exists(candidate, error) && !error) {
                    error.clear();
                    fs::path absolute = fs::absolute(candidate, error);
                    return error ? candidate : absolute;
                }
            }
            return {};
        }

    } // namespace

    bool openUrl(const std::string& url) {
        if (url.empty()) {
            return false;
        }

#if defined(_WIN32)
        HINSTANCE result = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<std::intptr_t>(result) > 32;
#elif defined(__APPLE__)
        const std::string command = "open " + shellQuote(url) + " >/dev/null 2>&1 &";
        return std::system(command.c_str()) == 0;
#else
        const std::string command = "xdg-open " + shellQuote(url) + " >/dev/null 2>&1 &";
        return std::system(command.c_str()) == 0;
#endif
    }

    bool initializeTray(const TrayOptions& options) {
        TrayState& state = trayState();
        if (state.initialized) {
            return true;
        }

        const std::filesystem::path resolvedIcon = resolveIconPath(options.iconPath);
        state.iconPath = resolvedIcon.empty() ? options.iconPath : resolvedIcon.string();

        if (!eui_tray_init(state.iconPath.c_str())) {
            state = {};
            return false;
        }

        (void)options.tooltip;
        state.initialized = true;
        return true;
    }

    bool isTrayInitialized() {
        return trayState().initialized && eui_tray_is_initialized();
    }

    void pollTray(bool blocking) {
        if (isTrayInitialized()) {
            eui_tray_poll(blocking ? 1 : 0);
        }
    }

    bool consumeTrayShowRequested() {
        return eui_tray_consume_show_requested() != 0;
    }

    bool consumeTrayExitRequested() {
        return eui_tray_consume_exit_requested() != 0;
    }

    void shutdownTray() {
        TrayState& state = trayState();
        if (state.initialized) {
            eui_tray_shutdown();
        }
        state = {};
    }

    void setImeCursorRect(GLFWwindow* window, float x, float y, float width, float height) {
        eui_ime_set_cursor_rect(window, x, y, width, height);
    }

} // namespace core::platform
