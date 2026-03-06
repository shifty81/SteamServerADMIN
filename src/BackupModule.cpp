#include "BackupModule.hpp"

#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>

// ---------------------------------------------------------------------------
// Zip helpers – use platform-native tools via QProcess so we don't need an
// additional library dependency.
//   Windows  : PowerShell Compress-Archive / Expand-Archive
//              Paths are passed as PowerShell script variables to avoid any
//              special-character injection in the -Command string.
//   Linux/Mac: zip / unzip (paths passed as separate QProcess arguments)
// ---------------------------------------------------------------------------

// Escape a path for use in a PowerShell double-quoted string.
// Only double-quotes and backticks need escaping inside "…".
static QString escapePwshArg(const QString &path)
{
    QString s = path;
    s.replace(QLatin1Char('`'),  QStringLiteral("``"));
    s.replace(QLatin1Char('"'),  QStringLiteral("`\""));
    s.replace(QLatin1Char('$'),  QStringLiteral("`$"));
    return s;
}

bool BackupModule::createZip(const QString &sourceDir, const QString &destZip,
                             int compressionLevel)
{
    QFileInfo srcInfo(sourceDir);
    if (!srcInfo.exists() || !srcInfo.isDir()) {
        qWarning() << "BackupModule::createZip: source dir does not exist:" << sourceDir;
        return false;
    }

    // Clamp to valid range
    if (compressionLevel < 0) compressionLevel = 0;
    if (compressionLevel > 9) compressionLevel = 9;

    // Ensure destination parent directory exists
    QDir().mkpath(QFileInfo(destZip).absolutePath());

    QProcess process;
#ifdef Q_OS_WIN
    // Assign paths to PowerShell variables first to avoid injection.
    // PowerShell Compress-Archive does not expose a compression level
    // parameter, so we use the Optimal level when compressionLevel > 0
    // and NoCompression (store only) when compressionLevel == 0.
    QString levelArg = (compressionLevel == 0)
                           ? QStringLiteral("NoCompression")
                           : QStringLiteral("Optimal");
    QString script = QStringLiteral(
        "$src = \"%1\\*\"; $dst = \"%2\"; "
        "Compress-Archive -LiteralPath $src -DestinationPath $dst -CompressionLevel %3 -Force")
        .arg(escapePwshArg(QDir::toNativeSeparators(sourceDir)),
             escapePwshArg(QDir::toNativeSeparators(destZip)),
             levelArg);
    QString cmd = QStringLiteral("powershell");
    QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-Command"),
        script
    };
#else
    QString cmd = QStringLiteral("zip");
    QStringList args = {
        QStringLiteral("-r"),
        QStringLiteral("-%1").arg(compressionLevel),
        destZip,
        QStringLiteral(".")
    };
    process.setWorkingDirectory(sourceDir);
#endif
    process.start(cmd, args);
    if (!process.waitForFinished(120000)) {
        qWarning() << "BackupModule::createZip: process timed out";
        return false;
    }
    if (process.exitCode() != 0) {
        qWarning() << "BackupModule::createZip: process failed:"
                   << process.readAllStandardError();
        return false;
    }
    return true;
}

bool BackupModule::extractZip(const QString &zipFile, const QString &destDir)
{
    QDir().mkpath(destDir);

    QProcess process;
#ifdef Q_OS_WIN
    QString script = QStringLiteral(
        "$src = \"%1\"; $dst = \"%2\"; "
        "Expand-Archive -LiteralPath $src -DestinationPath $dst -Force")
        .arg(escapePwshArg(QDir::toNativeSeparators(zipFile)),
             escapePwshArg(QDir::toNativeSeparators(destDir)));
    QString cmd = QStringLiteral("powershell");
    QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-Command"),
        script
    };
#else
    QString cmd = QStringLiteral("unzip");
    QStringList args = { QStringLiteral("-o"), zipFile, QStringLiteral("-d"), destDir };
#endif
    process.start(cmd, args);
    if (!process.waitForFinished(120000)) {
        qWarning() << "BackupModule::extractZip: process timed out";
        return false;
    }
    if (process.exitCode() != 0) {
        qWarning() << "BackupModule::extractZip: process failed:"
                   << process.readAllStandardError();
        return false;
    }
    return true;
}

QString BackupModule::takeSnapshot(const ServerConfig &server)
{
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString base      = server.backupFolder + QDir::separator() + timestamp;

    QDir().mkpath(server.backupFolder);

    bool ok = true;

    QString configDir = server.dir + QStringLiteral("/Configs");
    if (QDir(configDir).exists())
        ok &= createZip(configDir, base + QStringLiteral("_config.zip"), server.backupCompressionLevel);

    QString mapDir = server.dir + QStringLiteral("/Maps");
    if (QDir(mapDir).exists())
        ok &= createZip(mapDir, base + QStringLiteral("_map.zip"), server.backupCompressionLevel);

    QString modsDir = server.dir + QStringLiteral("/Mods");
    if (QDir(modsDir).exists())
        ok &= createZip(modsDir, base + QStringLiteral("_mods.zip"), server.backupCompressionLevel);

    if (!ok) {
        qWarning() << "BackupModule::takeSnapshot: one or more parts failed for" << server.name;
        return {};
    }

    rotateBackups(server);
    return timestamp;
}

bool BackupModule::restoreSnapshot(const QString &zipFile, const ServerConfig &server)
{
    // Determine target sub-directory from zip file name suffix
    QFileInfo fi(zipFile);
    QString base = fi.completeBaseName(); // e.g. "20260301_120000_config"

    QString destDir;
    if (base.endsWith(QLatin1String("_config")))
        destDir = server.dir + QStringLiteral("/Configs");
    else if (base.endsWith(QLatin1String("_map")))
        destDir = server.dir + QStringLiteral("/Maps");
    else if (base.endsWith(QLatin1String("_mods")))
        destDir = server.dir + QStringLiteral("/Mods");
    else
        destDir = server.dir;

    return extractZip(zipFile, destDir);
}

QStringList BackupModule::listSnapshots(const ServerConfig &server)
{
    QDir dir(server.backupFolder);
    if (!dir.exists())
        return {};

    QStringList files = dir.entryList({ QStringLiteral("*.zip") },
                                      QDir::Files, QDir::Name | QDir::Reversed);
    QStringList result;
    result.reserve(files.size());
    for (const QString &f : std::as_const(files))
        result << dir.absoluteFilePath(f);
    return result;
}

void BackupModule::rotateBackups(const ServerConfig &server)
{
    // Group files by type suffix and keep only keepBackups of each
    QStringList all = listSnapshots(server);
    QStringList types = { QStringLiteral("_config.zip"),
                          QStringLiteral("_map.zip"),
                          QStringLiteral("_mods.zip") };

    for (const QString &suffix : std::as_const(types)) {
        QStringList typed;
        for (const QString &f : std::as_const(all)) {
            if (f.endsWith(suffix))
                typed << f;
        }
        // Already sorted newest-first
        for (int i = server.keepBackups; i < typed.size(); ++i)
            QFile::remove(typed.at(i));
    }
}
