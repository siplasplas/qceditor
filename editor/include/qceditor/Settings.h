#pragma once
#include <QString>
#include <QVariant>

namespace qceditor {

/// Where the editor keeps its settings. Each program provides its own, so
/// that the settings stay where the program keeps the rest of its own (e.g.
/// QSettings in qceditor, a TOML configuration in gemini-commander). Keys
/// are paths such as "spelling/enabled".
class Settings
{
public:
    virtual ~Settings() = default;
    virtual QVariant value(const QString& key, const QVariant& defaultValue = {}) const = 0;
    virtual void setValue(const QString& key, const QVariant& value) = 0;
    virtual void remove(const QString& key) = 0;
};

/// The application's QSettings (its organization and application name).
class QSettingsStore : public Settings
{
public:
    QVariant value(const QString& key, const QVariant& defaultValue = {}) const override;
    void setValue(const QString& key, const QVariant& value) override;
    void remove(const QString& key) override;
};

/// The settings the editor uses: a QSettingsStore unless the program has
/// set its own.
Settings& settings();
/// Sets the program's settings (not owned; must outlive the editor);
/// nullptr goes back to QSettingsStore.
void setSettings(Settings* settings);

} // namespace qceditor
