#include <QApplication>
#include "client.h"
#include <gst/gst.h>

int main(int argc, char *argv[]) {
    gst_init(nullptr, nullptr); // Only once per process

    QApplication app(argc, argv);

    ClientWidget w("127.0.0.1", 5000, "127.0.0.1", 5001);
    w.resize(640, 480);
    w.show();

    return app.exec();
}
