#include "WebhookModule.hpp"
#include "ServerConfig.hpp"

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

static std::string currentTimestampISO()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

std::string WebhookModule::formatMessage(const std::string &tpl,
                                         const std::string &serverName,
                                         const std::string &event)
{
    std::string msg = tpl;
    msg = replaceAll(msg, "{server}",    serverName);
    msg = replaceAll(msg, "{event}",     event);
    msg = replaceAll(msg, "{timestamp}", currentTimestampISO());
    return msg;
}

// Escape a string for embedding in a JSON string value.
static std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

void WebhookModule::sendNotification(const std::string &webhookUrl,
                                     const std::string &serverName,
                                     const std::string &message,
                                     const std::string &messageTemplate)
{
    if (trimString(webhookUrl).empty())
        return;

    std::string content;
    if (!trimString(messageTemplate).empty())
        content = formatMessage(messageTemplate, serverName, message);
    else
        content = "**[" + serverName + "]** " + message;

    // Build JSON payload
    std::string payload = "{\"content\":\"" + jsonEscape(content) + "\"}";

    // Fire-and-forget: launch curl in a detached thread
    std::string url = webhookUrl;
    std::thread([url, payload]() {
        // Build the curl command
        std::string cmd = "curl -s -o /dev/null -X POST"
                          " -H \"Content-Type: application/json\""
                          " -d '" + payload + "'"
                          " \"" + url + "\" 2>/dev/null";
#ifdef _WIN32
        // On Windows, redirect to NUL
        cmd = "curl -s -o NUL -X POST"
              " -H \"Content-Type: application/json\""
              " -d \"" + payload + "\""
              " \"" + url + "\" 2>NUL";
#endif
        int ret = std::system(cmd.c_str());
        if (ret != 0)
            std::cerr << "WebhookModule: delivery failed (curl exit " << ret << ")\n";
    }).detach();
}
