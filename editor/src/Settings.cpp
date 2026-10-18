#include <qceditor/Settings.h>

#include <QSettings>

namespace qceditor {

QVariant QSettingsStore::value(const QString& key, const QVariant& defaultValue) const
{
    return QSettings().value(key, defaultValue);
}

void QSettingsStore::setValue(const QString& key, const QVariant& value)
{
    QSettings().setValue(key, value);
}

void QSettingsStore::remove(const QString& key)
{
    QSettings().remove(key);
}

namespace {
Settings* g_settings = nullptr;
}

Settings& settings()
{
    static QSettingsStore defaultStore;
    return g_settings ? *g_settings : defaultStore;
}

void setSettings(Settings* settings)
{
    g_settings = settings;
}

} // namespace qceditor
