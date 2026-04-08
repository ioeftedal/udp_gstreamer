#include <QApplication>
#include "client.h"
#include <gst/gst.h>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

struct ClientConfig {
    QString mainHost;
    quint16 mainPort;
    QString insetHost;
    quint16 insetPort;
};

bool loadClientConfig(const QString &filePath, ClientConfig &cfg) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open config file:" << filePath;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object()["client"].toObject();
    QJsonObject mainObj = root["main_stream"].toObject();
    QJsonObject insetObj = root["inset_stream"].toObject();

    cfg.mainHost = mainObj["host"].toString();
    cfg.mainPort = static_cast<quint16>(mainObj["port"].toInt());
    cfg.insetHost = insetObj["host"].toString();
    cfg.insetPort = static_cast<quint16>(insetObj["port"].toInt());

    return true;
}

int main(int argc, char *argv[]) {
    gst_init(nullptr, nullptr); // Only once per process
    QApplication app(argc, argv);

    ClientConfig cfg;
    if (!loadClientConfig("config.json", cfg)) return 1;

    ClientWidget w(cfg.mainHost, cfg.mainPort,
                   cfg.insetHost, cfg.insetPort);
    w.resize(640, 480);
    w.show();

    return app.exec();
}
