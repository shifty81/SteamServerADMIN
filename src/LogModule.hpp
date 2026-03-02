#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QFile>
#include <QMutex>
#include <QDateTime>

/**
 * @brief Centralized logging module that records all server operations.
 *
 * Writes timestamped log entries to a file and keeps a bounded in-memory
 * buffer so the GUI log-viewer tab can show recent history.
 */
class LogModule : public QObject {
    Q_OBJECT
public:
    explicit LogModule(const QString &logFilePath, QObject *parent = nullptr);
    ~LogModule() override;

    /** Append a timestamped entry to the log. Thread-safe. */
    void log(const QString &serverName, const QString &message);

    /** Return the in-memory log buffer (most-recent entries). */
    QStringList entries() const;

    /** Maximum number of in-memory entries (default 500). */
    int maxEntries() const;
    void setMaxEntries(int max);

    /** Path of the log file on disk. */
    QString logFilePath() const;

signals:
    /** Emitted every time a new log line is appended. */
    void entryAdded(const QString &formattedLine);

private:
    QString     m_logFilePath;
    QFile       m_file;
    QStringList m_entries;
    int         m_maxEntries = 500;
    mutable QMutex m_mutex;
};
