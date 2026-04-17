#pragma once
#include <QPoint>
class QEvent;

struct Ev {
    static QPoint global(QEvent* e);
    static QPoint local(QEvent* e);
};
