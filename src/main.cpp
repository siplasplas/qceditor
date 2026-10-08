#include <QApplication>
#include <qce/encoding/Encoding.h>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("QCEditor");
    app.setOrganizationName("qceditor");
    // Encoding/language models load in the background while the window opens.
    qce::encoding::preloadDetectionData();

    MainWindow window;
    window.show();

    for (int i = 1; i < argc; ++i)
        window.openFile(QString::fromLocal8Bit(argv[i]));

    return app.exec();
}
