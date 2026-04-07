#include <QApplication>
#include "client.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // IP and ports for your two camera streams
    ClientWidget w("127.0.0.1", 5000, "127.0.0.1", 5001);
    w.resize(640, 480);
    w.show();

    return app.exec();
}
