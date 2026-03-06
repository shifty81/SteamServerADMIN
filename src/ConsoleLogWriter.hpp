#pragma once

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QString>
#include <QMap>

/**
 * @brief Appends RCON console I/O to per-server timestamped log files.
 *
 * When console logging is enabled for a server, every command sent and every
 * response received is recorded under:
 *   <server.dir>/ConsoleLogs/<YYYYMMDD>.log
 */
class ConsoleLogWriter {
public:
    /**
     * @brief Append a line to the console log file for the given server.
     * @param serverDir  The server installation directory.
     * @param serverName The server name (used in header).
     * @param line       The text to append (command or response).
     */
    static void append(const QString &serverDir, const QString &serverName,
                       const QString &line)
    {
        QString logDir = serverDir + QStringLiteral("/ConsoleLogs");
        QDir().mkpath(logDir);

        QString dateStr = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"));
        QString logPath = logDir + QDir::separator() + dateStr + QStringLiteral(".log");

        QFile file(logPath);
        if (!file.open(QIODevice::Append | QIODevice::Text))
            return;

        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString(Qt::ISODate)
            << QStringLiteral(" [") << serverName << QStringLiteral("] ")
            << line << QStringLiteral("\n");
    }

    /**
     * @brief List available console log files for a server, newest first.
     */
    static QStringList listLogs(const QString &serverDir)
    {
        QDir dir(serverDir + QStringLiteral("/ConsoleLogs"));
        if (!dir.exists())
            return {};
        return dir.entryList({QStringLiteral("*.log")},
                             QDir::Files, QDir::Name | QDir::Reversed);
    }
};
