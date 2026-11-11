// An editor tab searching through the find bar module (spp-findbar, built
// along with the application), offscreen: seeking and Replace All as one
// undo step, no replacing in a read-only tab, the tab's own bar without the
// module.

#include <qceditor/EditorTab.h>
#include <qceditor/FindBar.h>
#include <qceditor/FindPlugin.h>

#include "siplasplas/plugin/FindPlugin.h"

#include <qce/CodeEditArea.h>

#include <QFile>
#include <QLineEdit>
#include <QPluginLoader>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

namespace {

QLineEdit* field(QWidget* tab, const QString& placeholder)
{
    for (auto* edit : tab->findChildren<QLineEdit*>())
        if (edit->placeholderText() == placeholder) return edit;
    return nullptr;
}

QPushButton* button(QWidget* tab, const QString& text)
{
    for (auto* push : tab->findChildren<QPushButton*>())
        if (push->text() == text) return push;
    return nullptr;
}

} // namespace

class FindPluginTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_loader.setFileName(QStringLiteral(SPP_FINDBAR_PLUGIN_FILE));
        m_plugin = qobject_cast<spp::FindPlugin*>(m_loader.instance());
        QVERIFY2(m_plugin, qPrintable(m_loader.errorString()));
    }

    void replacesAllAsOneUndoStep()
    {
        qceditor::setFindPlugin(m_plugin);
        qceditor::EditorTab tab;
        QVERIFY(tab.loadFile(write("a=1; b=2; c=3;\n")));
        QVERIFY(!tab.findChild<qceditor::FindBar*>());  // the module's bar instead
        tab.showReplace();
        QLineEdit* find = field(&tab, QStringLiteral("Find"));
        QLineEdit* replacement = field(&tab, QStringLiteral("Replace with"));
        QVERIFY(find && replacement);
        find->setText(QStringLiteral("="));
        emit find->textEdited(find->text());
        replacement->setText(QStringLiteral(":"));
        QPushButton* replaceAll = button(&tab, QStringLiteral("Replace All"));
        QVERIFY(replaceAll);
        replaceAll->click();
        QCOMPARE(tab.document()->toPlainText(), QStringLiteral("a:1; b:2; c:3;"));
        tab.editor()->area()->undoStack()->undo();
        QCOMPARE(tab.document()->toPlainText(), QStringLiteral("a=1; b=2; c=3;"));
    }

    void noReplacingInAReadOnlyTab()
    {
        qceditor::setFindPlugin(m_plugin);
        qceditor::EditorTab tab;
        QVERIFY(tab.loadFile(write("one one\n")));
        tab.setReadOnly(true);
        tab.showReplace();
        QPushButton* replaceAll = button(&tab, QStringLiteral("Replace All"));
        QVERIFY(replaceAll);
        QVERIFY(replaceAll->parentWidget()->isHidden());
        // F3 finds, the text stays.
        QLineEdit* find = field(&tab, QStringLiteral("Find"));
        find->setText(QStringLiteral("one"));
        emit find->textEdited(find->text());
        tab.findNext();
        QVERIFY(tab.editor()->area()->hasSelection());
        QCOMPARE(tab.document()->toPlainText(), QStringLiteral("one one"));
    }

    void ownBarWithoutTheModule()
    {
        qceditor::setFindPlugin(nullptr);
        qceditor::EditorTab tab;
        QVERIFY(tab.findChild<qceditor::FindBar*>());
    }

private:
    QString write(const QByteArray& text)
    {
        const QString path = m_dir.filePath(QStringLiteral("text%1.txt").arg(++m_files));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) file.write(text);
        return path;
    }

    QPluginLoader m_loader;
    spp::FindPlugin* m_plugin = nullptr;
    QTemporaryDir m_dir;
    int m_files = 0;
};

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    FindPluginTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_find_plugin.moc"
