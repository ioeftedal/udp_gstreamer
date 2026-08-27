#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include "client.h"
#include <gst/gst.h>

#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>


/**
 * @brief Holds configuration parameters for the client streams.
 *
 * Separates configuration from runtime logic so the widget can be
 * instantiated cleanly with already-parsed host/port information.
 */
struct ClientConfig {
    QString mainHost;   ///< Main stream host
    quint16 mainPort;   ///< Main stream UDP port
    QString insetHost;  ///< Inset stream host
    quint16 insetPort;  ///< Inset stream UDP port
    QString cmdHost;    ///< Command server host
    quint16 cmdPort;    ///< Command server UDP port
};

/**
 * @brief Load client configuration from JSON file.
 *
 * Expects a structure like:
 * {
 *   "client": {
 *       "main_stream": { "host": "...", "port": 5000 },
 *       "inset_stream": { "host": "...", "port": 5001 },
 *       "commands": { "host": "...", "port": 5002 }
 *   }
 * }
 *
 * @param filePath Path to JSON configuration file.
 * @param cfg Output configuration structure.
 * @return true if configuration loaded successfully; false otherwise.
 *
 * Why: Keeping configuration loading separate improves testability
 * and allows future changes (e.g., different formats, network configs)
 * without touching the widget logic.
 */
bool loadClientConfig(const QString &filePath, ClientConfig &cfg) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open config file:" << filePath;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;

    QJsonObject clientObj = doc.object().value("client").toObject();
    if (clientObj.isEmpty()) return false;

    QJsonObject mainObj = clientObj.value("main_stream").toObject();
    QJsonObject insetObj = clientObj.value("inset_stream").toObject();
    QJsonObject cmdObj = clientObj.value("commands").toObject();

    if (mainObj.isEmpty() || insetObj.isEmpty() || cmdObj.isEmpty()) return false;

    cfg.mainHost = mainObj.value("host").toString();
    cfg.mainPort = static_cast<quint16>(mainObj.value("port").toInt());
    cfg.insetHost = insetObj.value("host").toString();
    cfg.insetPort = static_cast<quint16>(insetObj.value("port").toInt());
    cfg.cmdHost = cmdObj.value("host").toString();
    cfg.cmdPort = static_cast<quint16>(cmdObj.value("port").toInt());

    return true;
}

/**
 * @brief Application entry point.
 *
 * Why:
 *  - Initializes GStreamer globally (needed before creating pipelines)
 *  - Initializes Qt application
 *  - Loads configuration separately
 *  - Creates ClientWidget with proper host/port
 */
int main(int argc, char *argv[]) {
    // Initialize GStreamer library first (required by all pipelines)
    gst_init(nullptr, nullptr);

    // Initialize Qt application
    QApplication app(argc, argv);

    // Load client configuration
    ClientConfig cfg;
    if (!loadClientConfig("config.json", cfg)) {
        qCritical() << "Failed to load configuration. Exiting.";
        return 1;
    }

    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));

    // Fill server address info from config
    servaddr.sin_family = AF_INET;              // IPv4
    servaddr.sin_port   = htons(cfg.cmdPort);   // Command server port from config
    servaddr.sin_addr.s_addr = inet_addr(cfg.cmdHost.toStdString().c_str()); // Command server IP from config

    // Create main window widget for video streams with command socket
    ClientWidget w(cfg.mainHost, cfg.mainPort, cfg.insetHost, cfg.insetPort,
                   sockfd, servaddr);
    w.resize(800, 600); // Initial size
    w.show();           // Display the widget

    // Enter Qt main event loop
    int result = app.exec();

    // Cleanup socket before exiting
    close(sockfd);
    return result;
}
