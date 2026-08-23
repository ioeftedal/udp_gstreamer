#include "client.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QMouseEvent>
#include <QDebug>
#include <sys/socket.h>
#include <string.h>

ClientWidget::ClientWidget(const QString &hostMain, quint16 portMain,
                           const QString &hostInset, quint16 portInset,
                           int cmdSocket, const struct sockaddr_in &cmdAddr,
                           QWidget *parent)
    : QWidget(parent),
      exposureValue_(0),
      shotCount_(0),
      mainName_("Feed 1"),
      secondaryName_("Feed 2"),
      cmdSocket_(cmdSocket),
      cmdAddr_(cmdAddr)
{
    // Initialize atomic targets
    mainTarget_.store(nullptr);
    secondaryTarget_.store(nullptr);

    // Set dark background for the entire widget
    setStyleSheet("background-color: #0a0b0c;");

    // Main horizontal layout: video view + sidebar
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Create and add main video view
    QWidget *videoView = createMainVideoView();
    mainLayout->addWidget(videoView, 1); // Stretch factor 1

    // Create and add sidebar
    QWidget *sidebar = createSidebar();
    mainLayout->addWidget(sidebar, 0); // No stretch

    setLayout(mainLayout);

    // Start streaming pipelines
    startPipeline(hostMain, portMain, true);
    startPipeline(hostInset, portInset, false);

    // Set atomic targets after labels are created
    mainTarget_.store(mainVideoLabel_);
    secondaryTarget_.store(secondaryVideoLabel_);
}

ClientWidget::~ClientWidget() = default;

QWidget* ClientWidget::createMainVideoView() {
    // Container for the main video feed
    QWidget *container = new QWidget();
    container->setStyleSheet("background-color: #000000;");

    // Main video label fills the entire container
    mainVideoLabel_ = new QLabel(container);
    mainVideoLabel_->setStyleSheet("background-color: #000000;");
    mainVideoLabel_->setScaledContents(true);
    mainVideoLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    // Position video label to fill container
    auto updateVideoLabel = [this, container]() {
        mainVideoLabel_->setGeometry(0, 0, container->width(), container->height());
    };

    container->installEventFilter(this);
    connect(container, &QWidget::destroyed, updateVideoLabel);
    QTimer::singleShot(0, updateVideoLabel);

    return container;
}

QWidget* ClientWidget::createSidebar() {
    QWidget *sidebar = new QWidget();
    sidebar->setFixedWidth(340);
    sidebar->setStyleSheet("background-color: #2d2e30;");

    QVBoxLayout *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(20);

    // --- SECONDARY VIDEO SECTION ---
    QVBoxLayout *secVideoLayout = new QVBoxLayout();
    secVideoLayout->setSpacing(10);

    // Header
    QHBoxLayout *secHeaderLayout = new QHBoxLayout();
    QLabel *secLabel = new QLabel("SECONDARY");
    secLabel->setStyleSheet("color: rgba(150, 150, 150, 255); font-family: 'Courier New', monospace; "
                            "font-size: 10px; letter-spacing: 2px;");
    QLabel *secNameLabel = new QLabel(secondaryName_);
    secNameLabel->setStyleSheet("color: rgba(180, 180, 180, 255); font-family: 'Courier New', monospace; "
                                "font-size: 10px; letter-spacing: 2px;");
    secHeaderLayout->addWidget(secLabel);
    secHeaderLayout->addStretch();
    secHeaderLayout->addWidget(secNameLabel);
    secVideoLayout->addLayout(secHeaderLayout);

    // Secondary video preview
    secondaryVideoLabel_ = new QLabel();
    secondaryVideoLabel_->setFixedHeight(170); // 16:9 aspect ratio for 304px width
    secondaryVideoLabel_->setStyleSheet(
        "background-color: #1e1f21; "
        "border: 1px solid #484848; "
        "background-image: repeating-linear-gradient(135deg, #363738 0px, #363738 2px, #282829 2px, #282829 12px);"
    );
    secondaryVideoLabel_->setScaledContents(true);
    secondaryVideoLabel_->setAlignment(Qt::AlignCenter);
    secondaryVideoLabel_->setCursor(Qt::PointingHandCursor);
    secondaryVideoLabel_->installEventFilter(this);

    // Add placeholder text
    QLabel *secPlaceholder = new QLabel(secondaryName_ + " VIDEO", secondaryVideoLabel_);
    secPlaceholder->setStyleSheet("color: rgba(120, 120, 120, 255); font-family: 'Courier New', monospace; "
                                  "font-size: 10px; letter-spacing: 2px; background-color: transparent;");
    secPlaceholder->setAlignment(Qt::AlignCenter);
    secPlaceholder->setGeometry(0, 70, 304, 20);
        
    secVideoLayout->addWidget(secondaryVideoLabel_);

    // Swap button
    swapButton_ = new QPushButton("SWITCH CAMERA FEED");
    swapButton_->setStyleSheet(
        "QPushButton { "
        "  background-color: transparent; "
        "  border: 1px solid #4d4d4d; "
        "  color: rgba(210, 210, 210, 255); "
        "  font-family: 'Courier New', monospace; "
        "  font-size: 11px; "
        "  letter-spacing: 2px; "
        "  padding: 11px 12px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #3d3e40; "
        "  border-color: #6a6a6a; "
        "}"
    );
    connect(swapButton_, &QPushButton::clicked, this, &ClientWidget::swapVideos);
    secVideoLayout->addWidget(swapButton_);

    layout->addLayout(secVideoLayout);

    // --- EXPOSURE CONTROL SECTION ---
    QVBoxLayout *expLayout = new QVBoxLayout();
    expLayout->setSpacing(12);

    // Exposure header
    QHBoxLayout *expHeaderLayout = new QHBoxLayout();
    QLabel *expHeaderLabel = new QLabel("EXPOSURE");
    expHeaderLabel->setStyleSheet("color: rgba(150, 150, 150, 255); font-family: 'Courier New', monospace; "
                                  "font-size: 10px; letter-spacing: 2px;");

    exposureLabel_ = new QLabel("0.0 EV");
    exposureLabel_->setStyleSheet("color: rgba(235, 235, 235, 255); font-family: 'Courier New', monospace; "
                                  "font-size: 16px; font-weight: 500; letter-spacing: 1px;");
    expHeaderLayout->addWidget(expHeaderLabel);
    expHeaderLayout->addStretch();
    expHeaderLayout->addWidget(exposureLabel_);
    expLayout->addLayout(expHeaderLayout);

    // Exposure controls
    QHBoxLayout *expControlLayout = new QHBoxLayout();

    QString buttonStyle =
        "QPushButton { "
        "  background-color: transparent; "
        "  border: 1px solid #4d4d4d; "
        "  color: rgba(210, 210, 210, 255); "
        "  font-family: 'Courier New', monospace; "
        "  font-size: 14px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #3d3e40; "
        "}";

    expDownButton_ = new QPushButton("−");
    expDownButton_->setFixedWidth(38);
    expDownButton_->setStyleSheet(buttonStyle);
    connect(expDownButton_, &QPushButton::clicked, this, [this]() { adjustExposure(-1); });

    expUpButton_ = new QPushButton("+");
    expUpButton_->setFixedWidth(38);
    expUpButton_->setStyleSheet(buttonStyle);
    connect(expUpButton_, &QPushButton::clicked, this, [this]() { adjustExposure(1); });

    // Visual exposure bar (placeholder)
    QLabel *expBar = new QLabel();
    expBar->setFixedHeight(38);
    expBar->setStyleSheet("background-color: #1e1f21; border: 1px solid #484848;");

    expControlLayout->addWidget(expDownButton_);
    expControlLayout->addWidget(expBar, 1);
    expControlLayout->addWidget(expUpButton_);
    expLayout->addLayout(expControlLayout);

    // Exposure scale labels
    QHBoxLayout *expScaleLayout = new QHBoxLayout();
    QLabel *expMin = new QLabel("−2.0");
    QLabel *expZero = new QLabel("0");
    QLabel *expMax = new QLabel("+2.0");
    QString scaleStyle = "color: rgba(115, 115, 115, 255); font-family: 'Courier New', monospace; "
                        "font-size: 10px; letter-spacing: 1px;";
    expMin->setStyleSheet(scaleStyle);
    expZero->setStyleSheet(scaleStyle);
    expMax->setStyleSheet(scaleStyle);
    expScaleLayout->addWidget(expMin);
    expScaleLayout->addStretch();
    expScaleLayout->addWidget(expZero);
    expScaleLayout->addStretch();
    expScaleLayout->addWidget(expMax);
    expLayout->addLayout(expScaleLayout);

    layout->addLayout(expLayout);

    // --- SHOOT BUTTON ---
    QPushButton *shootButton = new QPushButton("SHOOT");
    shootButton->setStyleSheet(
        "QPushButton { "
        "  background-color: #c99a2e; "
        "  border: 1px solid #d4a840; "
        "  color: rgba(20, 20, 20, 255); "
        "  font-family: 'Courier New', monospace; "
        "  font-size: 12px; "
        "  font-weight: 600; "
        "  letter-spacing: 2px; "
        "  padding: 14px 12px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #d4a840; "
        "  border-color: #e0b652; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #b38926; "
        "}"
    );
    connect(shootButton, &QPushButton::clicked, this, [this]() {
        shotCount_++;
        sendCommand("shoot");
    });
    layout->addWidget(shootButton);

    // Add stretch to push everything to the top
    layout->addStretch();

    return sidebar;
}

void ClientWidget::swapVideos() {
    QLabel* oldMain = mainTarget_.exchange(secondaryTarget_);
    secondaryTarget_.store(oldMain);

    // Swap names
    std::swap(mainName_, secondaryName_);
}

void ClientWidget::adjustExposure(int delta) {
    exposureValue_ += delta;
    exposureValue_ = qBound(-20, exposureValue_, 20);

    double ev = exposureValue_ / 10.0;
    exposureLabel_->setText(QString::number(ev, 'f', 1) + " EV");
}

void ClientWidget::sendCommand(const QString &command) {
    QByteArray cmdBytes = command.toUtf8();
    ssize_t sent = sendto(cmdSocket_, cmdBytes.constData(), cmdBytes.size(),
                          MSG_CONFIRM,
                          (const struct sockaddr *)&cmdAddr_, sizeof(cmdAddr_));

    if (sent < 0) {
        qWarning() << "Failed to send command:" << command;
    } else {
        qDebug() << "Command sent:" << command;
    }
}

bool ClientWidget::eventFilter(QObject *obj, QEvent *event) {
    if (obj == secondaryVideoLabel_ && event->type() == QEvent::MouseButtonRelease) {
        swapVideos();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void ClientWidget::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);

    // Draw crosshair reticle on main video
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate center of main video area (accounting for sidebar width)
    int videoWidth = width() - 340;
    int centerX = videoWidth / 2;
    int centerY = height() / 2;

    // Reticle size
    const int reticleSize = 220;
    const int halfSize = reticleSize / 2;

    // Set pen for reticle
    QPen reticlePen(QColor(201, 154, 46, 180), 1); // Golden/yellowish color
    QPen faintPen(QColor(100, 100, 100, 100), 1);

    // Draw crosshair lines
    painter.setPen(faintPen);
    painter.drawLine(centerX - halfSize, centerY, centerX + halfSize, centerY); // Horizontal
    painter.drawLine(centerX, centerY - halfSize, centerX, centerY + halfSize); // Vertical

    // Draw corner brackets
    painter.setPen(reticlePen);
    const int cornerSize = 22;

    // Top-left
    painter.drawLine(centerX - halfSize, centerY - halfSize,
                    centerX - halfSize + cornerSize, centerY - halfSize);
    painter.drawLine(centerX - halfSize, centerY - halfSize,
                    centerX - halfSize, centerY - halfSize + cornerSize);

    // Top-right
    painter.drawLine(centerX + halfSize, centerY - halfSize,
                    centerX + halfSize - cornerSize, centerY - halfSize);
    painter.drawLine(centerX + halfSize, centerY - halfSize,
                    centerX + halfSize, centerY - halfSize + cornerSize);

    // Bottom-left
    painter.drawLine(centerX - halfSize, centerY + halfSize,
                    centerX - halfSize + cornerSize, centerY + halfSize);
    painter.drawLine(centerX - halfSize, centerY + halfSize,
                    centerX - halfSize, centerY + halfSize - cornerSize);

    // Bottom-right
    painter.drawLine(centerX + halfSize, centerY + halfSize,
                    centerX + halfSize - cornerSize, centerY + halfSize);
    painter.drawLine(centerX + halfSize, centerY + halfSize,
                    centerX + halfSize, centerY + halfSize - cornerSize);

    // Draw center circle
    painter.drawEllipse(QPoint(centerX, centerY), 23, 23);
}

void ClientWidget::startPipeline(const QString &host, quint16 port, bool isMain) {
    QString pipelineDesc = QString(
        "udpsrc port=%1 ! "
        "application/x-rtp,encoding-name=H264,payload=96 ! "
        "rtph264depay ! avdec_h264 ! videoconvert ! "
        "video/x-raw,format=BGR ! appsink name=sink")
        .arg(port);

    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(pipelineDesc.toUtf8().constData(), &error);
    if (!pipeline) {
        qWarning() << "Failed to create pipeline:" << (error ? error->message : "Unknown");
        if (error) g_error_free(error);
        return;
    }

    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!appsink) {
        qWarning() << "Appsink not found!";
        gst_object_unref(pipeline);
        return;
    }

    gst_app_sink_set_emit_signals(GST_APP_SINK(appsink), true);
    gst_app_sink_set_max_buffers(GST_APP_SINK(appsink), 1);
    gst_app_sink_set_drop(GST_APP_SINK(appsink), true);
    g_object_set(appsink, "sync", FALSE, nullptr);

    g_signal_connect(appsink, "new-sample", G_CALLBACK(onNewSample), this);

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "Failed to set pipeline to PLAYING state";
        gst_object_unref(pipeline);
        return;
    }

    if (isMain) {
        mainPipeline_.reset(pipeline);
    } else {
        secondaryPipeline_.reset(pipeline);
    }
}

GstFlowReturn ClientWidget::onNewSample(GstAppSink *sink, gpointer user_data) {
    auto *self = static_cast<ClientWidget*>(user_data);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_OK;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    if (!caps) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstStructure *s = gst_caps_get_structure(caps, 0);
    int width = 0, height = 0;
    if (!gst_structure_get_int(s, "width", &width) ||
        !gst_structure_get_int(s, "height", &height)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    QImage img(map.data, width, height, QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(img.rgbSwapped().copy());
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    // Determine target label based on pipeline
    bool isMainStream = (GST_OBJECT_PARENT(sink) == GST_OBJECT(self->mainPipeline_.get()));
    QLabel* target = isMainStream ? self->mainTarget_.load() : self->secondaryTarget_.load();

    if (target) {
        QMetaObject::invokeMethod(target, "setPixmap", Qt::QueuedConnection,
                                  Q_ARG(QPixmap, pixmap));
    }

    return GST_FLOW_OK;
}
