#include "LogModule.hpp"

#include <QDir>
#include <QTextStream>
#include <QMutexLocker>

LogModule::LogModule(const QString &logFilePath, QObject *parent)
    : QObject(parent), m_logFilePath(logFilePath), m_file(logFilePath)
{
    // Ensure parent directory exists
    QDir().mkpath(QFileInfo(logFilePath).absolutePath());
    m_file.open(QIODevice::Append | QIODevice::Text);
}

LogModule::~LogModule()
{
    if (m_file.isOpen())
        m_file.close();
}

void LogModule::log(const QString &serverName, const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString line = QStringLiteral("[%1] [%2] %3").arg(timestamp, serverName, message);

    QMutexLocker locker(&m_mutex);

    // Append to in-memory buffer
    m_entries.append(line);
    while (m_entries.size() > m_maxEntries)
        m_entries.removeFirst();

    // Append to file
    if (m_file.isOpen()) {
        QTextStream out(&m_file);
        out << line << '\n';
        m_file.flush();
    }

    locker.unlock();
    emit entryAdded(line);
}

QStringList LogModule::entries() const
{
    QMutexLocker locker(&m_mutex);
    return m_entries;
}

int LogModule::maxEntries() const
{
    return m_maxEntries;
}

void LogModule::setMaxEntries(int max)
{
    QMutexLocker locker(&m_mutex);
    m_maxEntries = max;
    while (m_entries.size() > m_maxEntries)
        m_entries.removeFirst();
}

QString LogModule::logFilePath() const
{
    return m_logFilePath;
}
