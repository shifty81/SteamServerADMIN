#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief Platform-agnostic notification manager for SSA.
 *
 * Queues notifications so the ImGui UI layer can display them as
 * toast overlays.  Replaces the former Qt-based system-tray wrapper.
 */
class TrayManager {
public:
    struct Notification {
        std::string title;
        std::string message;
        std::chrono::steady_clock::time_point timestamp;
    };

    TrayManager() = default;

    /** Enqueue a notification for the ImGui layer to render. */
    void notify(const std::string &title, const std::string &message);

    /** Notifications are always available (rendered via ImGui toast). */
    bool isAvailable() const;

    /** True when at least one unread notification is pending. */
    bool hasNotifications() const;

    /** Return and clear all pending notifications. */
    std::vector<Notification> consumeNotifications();

    /** Set by the UI layer; invoked when the user requests an app quit. */
    std::function<void()> onQuitRequested;

private:
    mutable std::mutex m_mutex;
    std::vector<Notification> m_pending;
};
