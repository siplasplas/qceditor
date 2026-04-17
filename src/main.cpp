#include <QApplication>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("QCEditor");
    app.setOrganizationName("qceditor");

    MainWindow window;
    window.show();

    for (int i = 1; i < argc; ++i)
        window.openFile(QString::fromLocal8Bit(argv[i]));

    return app.exec();
}
