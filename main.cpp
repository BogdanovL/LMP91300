#include "mainwindow.h"
#include "chartwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    ChartWindow c;
    MainWindow w(&c);
    w.show();
    c.hide();

    return a.exec();
}
