#include "TrayManager.hpp"

#include <algorithm>
#include <utility>

void TrayManager::notify(const std::string &title, const std::string &message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.push_back({title, message, std::chrono::steady_clock::now()});
}

bool TrayManager::isAvailable() const
{
    return true;
}

bool TrayManager::hasNotifications() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_pending.empty();
}

std::vector<TrayManager::Notification> TrayManager::consumeNotifications()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Notification> result;
    result.swap(m_pending);
    return result;
}
