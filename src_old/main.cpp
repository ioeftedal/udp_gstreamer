// #include <QApplication>
// #include <QProgressBar>
// #include <QSlider>
//
// int main(int argc, char **argv)
// {
//  QApplication app (argc, argv);
//
//  // Create a container window
//  QWidget videocontainer;
//  videocontainer.setFixedSize(800, 800);
//
//  videocontainer.show();
//
//  return app.exec();
// }

// #include <QApplication>
//
// #include "videocontainer.h"
//
// int main(int argc, char *argv[])
// {
//     QApplication app(argc, argv);
//     Window window;
//     window.show();
//     return app.exec();
// }

#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPixmap>

int main(int argc, char **argv)
{
    QApplication a(argc, argv);
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    QGraphicsPixmapItem item(QPixmap("~/Downloads/drone.jpg"));
    scene.addItem(&item);
    view.show();
    return a.exec();
}
