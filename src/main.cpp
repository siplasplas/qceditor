#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <qce/encoding/Encoding.h>
#include <qxmigration.h>
#include "mainwindow.h"

namespace {

// Older qceditor versions kept the recent files in ~/.local/share/qceditor/config.json
// and the settings in ~/.config/qceditor/QCEditor.conf (organization "qceditor",
// application "QCEditor").
const char* kOldRecentFiles = "config.json";

QString oldSettingsFile()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("qceditor/QCEditor.conf"));
}

// What older versions wrote: config.json holding recentFiles, and only the keys below
// in the settings file.
bool recognizeOldFile(const QString& path)
{
    if (path.endsWith(QLatin1String(".conf"))) {
        const QStringList known = {QStringLiteral("syntaxData/declinedDownload")};
        const QSettings settings(path, QSettings::IniFormat);
        for (const QString& key : settings.allKeys()) {
            if (!known.contains(key))
                return false;
        }
        return true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() && document.object().contains(QStringLiteral("recentFiles"));
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Configuration goes to ~/.config/siplasplas/qceditor(.conf), data such as the recent
    // files to ~/.local/share/siplasplas/qceditor.
    app.setOrganizationName("siplasplas");
    app.setApplicationName("qceditor");
    app.setApplicationDisplayName("QCEditor");

    // Moves the files of older versions, once, before anything reads them.
    QxMigrationSpec migration;
    migration.dataEntries = {kOldRecentFiles};
    migration.renames = {{kOldRecentFiles, "recent-files.json"}};
    migration.oldSettingsFiles = {oldSettingsFile()};
    migration.recognize = recognizeOldFile;
    const QxMigrationResult moved = qxMigrateToSiplasplas(migration);
    for (const QString& line : moved.log)
        qInfo("Moving qceditor's files: %s", qPrintable(line));
    QString recentFilesPath;
    if (moved.status == QxMigrationResult::Failed) {
        // The old places for this run; the next start tries again.
        app.setOrganizationName("qceditor");
        app.setApplicationName("QCEditor");
        recentFilesPath = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                              .filePath(QStringLiteral("qceditor/config.json"));
    }

    // Encoding/language models load in the background while the window opens.
    qce::encoding::preloadDetectionData();

    MainWindow window(recentFilesPath);
    window.show();
    if (moved.status == QxMigrationResult::Migrated || moved.status == QxMigrationResult::Partial) {
        window.statusBar()->showMessage(
            QObject::tr("Moved QCEditor's files to ~/.config/siplasplas and ~/.local/share/siplasplas"),
            8000);
    } else if (moved.status == QxMigrationResult::Failed) {
        window.statusBar()->showMessage(
            QObject::tr("Could not move QCEditor's files: %1").arg(moved.log.value(moved.log.size() - 1)),
            15000);
    }

    for (int i = 1; i < argc; ++i)
        window.openFile(QString::fromLocal8Bit(argv[i]));

    return app.exec();
}
