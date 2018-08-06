#include "mainwindow.h"
#include "chartwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    ChartWindow c;
    c.hide();
    MainWindow w(&c);
    w.show();
    // No resize
    w.setFixedSize(w.size());

    return a.exec();
}
