#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <qce/encoding/Encoding.h>
#include <qce/kate/KatePaths.h>
#include <qceditor/EditorWindow.h>
#include <qceditor/Settings.h>
#include <qxmigration.h>

namespace {

// The editor's settings in the application's QSettings, except the recent
// files: data rather than configuration, kept in recent-files.json in the
// data directory (~/.local/share/siplasplas/qceditor) under "recentFiles".
class QceditorSettings : public qceditor::QSettingsStore
{
public:
    explicit QceditorSettings(QString recentFilesPath) : m_path(std::move(recentFilesPath)) {}

    QVariant value(const QString& key, const QVariant& defaultValue) const override
    {
        if (key != kRecentFiles) return QSettingsStore::value(key, defaultValue);
        const QJsonValue files = read().value(kRecentFiles);
        return files.isArray() ? QVariant(files.toVariant().toStringList()) : defaultValue;
    }
    void setValue(const QString& key, const QVariant& value) override
    {
        if (key != kRecentFiles) return QSettingsStore::setValue(key, value);
        QJsonObject json = read();
        json.insert(kRecentFiles, QJsonArray::fromStringList(value.toStringList()));
        write(json);
    }
    void remove(const QString& key) override
    {
        if (key != kRecentFiles) return QSettingsStore::remove(key);
        QJsonObject json = read();
        json.remove(kRecentFiles);
        write(json);
    }

private:
    static constexpr const char* kRecentFiles = "recentFiles";

    QJsonObject read() const
    {
        QFile file(m_path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            qWarning("Cannot read recent files: invalid JSON in %s", qPrintable(m_path));
            return {};
        }
        return document.object();
    }
    void write(const QJsonObject& json) const
    {
        if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
            qWarning("Cannot create data directory for %s", qPrintable(m_path));
            return;
        }
        QSaveFile file(m_path);
        const QByteArray bytes = QJsonDocument(json).toJson(QJsonDocument::Indented);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            qWarning("Cannot save recent files: %s", qPrintable(file.errorString()));
    }

    QString m_path;
};

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
    app.setApplicationVersion(QStringLiteral(QCEDITOR_VERSION));

    // Moves the files of older versions, once, before anything reads them.
    QxMigrationSpec migration;
    migration.dataEntries = {kOldRecentFiles};
    migration.renames = {{kOldRecentFiles, "recent-files.json"}};
    migration.oldSettingsFiles = {oldSettingsFile()};
    migration.recognize = recognizeOldFile;
    const QxMigrationResult moved = qxMigrateToSiplasplas(migration);
    for (const QString& line : moved.log)
        qInfo("Moving qceditor's files: %s", qPrintable(line));
    QString recentFilesPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                  .filePath(QStringLiteral("recent-files.json"));
    if (moved.status == QxMigrationResult::Failed) {
        // The old places for this run; the next start tries again.
        app.setOrganizationName("qceditor");
        app.setApplicationName("QCEditor");
        recentFilesPath = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                              .filePath(QStringLiteral("qceditor/config.json"));
    }

    // The Kate syntax data is qcodeedit's: up to qcodeedit 1.12 it was in
    // ~/.local/share/qcodeedit, now in the shared ~/.local/share/kate-data.
    const qce::kate::DataRootMigration kateMoved = qce::kate::migrateDataRoot();
    for (const QString& line : kateMoved.log)
        qInfo("Moving Kate data: %s", qPrintable(line));
    for (const QString& line : kateMoved.warnings)
        qWarning("Moving Kate data: %s", qPrintable(line));

    // Encoding/language models load in the background while the window opens.
    qce::encoding::preloadDetectionData();

    QceditorSettings settings(recentFilesPath);
    qceditor::setSettings(&settings);

    qceditor::EditorWindow window;
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
