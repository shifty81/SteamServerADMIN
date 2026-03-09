#pragma once

#include <string>
#include <functional>

/**
 * @brief Sends HTTP POST requests to Discord webhook URLs for event notifications.
 *
 * Notifications are non-blocking (fire-and-forget via a detached curl process).
 * An optional message template may contain placeholders:
 *   {server}    - server name
 *   {event}     - event description
 *   {timestamp} - current date/time (ISO 8601)
 */
class WebhookModule {
public:
    WebhookModule() = default;

    /** Send a notification to a Discord webhook. Non-blocking.
     *  @param messageTemplate  Optional custom template.  If empty, the
     *         default format "**[{server}]** {event}" is used. */
    void sendNotification(const std::string &webhookUrl,
                          const std::string &serverName,
                          const std::string &message,
                          const std::string &messageTemplate = "");

    /** Format a message using the template string. Exposed for testing. */
    static std::string formatMessage(const std::string &tpl,
                                     const std::string &serverName,
                                     const std::string &event);
};
