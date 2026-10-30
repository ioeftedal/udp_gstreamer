#include "client.h"
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <string.h>
#include <sys/socket.h>

ClientWidget::ClientWidget(const QString &hostMain, quint16 portMain,
                           const QString &hostInset, quint16 portInset,
                           int cmdSocket, const struct sockaddr_in &cmdAddr,
                           QWidget *parent)
    : QWidget(parent), shotCount_(0), mainName_("Feed 1"),
      secondaryName_("Feed 2"), cmdSocket_(cmdSocket), cmdAddr_(cmdAddr) {
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

QWidget *ClientWidget::createMainVideoView() {
  // Container for the main video feed
  QWidget *container = new QWidget();
  container->setStyleSheet("background-color: #000000;");

  // Main video label fills the entire container
  mainVideoLabel_ = new QLabel(container);
  mainVideoLabel_->setStyleSheet("background-color: #000000;");
  mainVideoLabel_->setAlignment(Qt::AlignCenter);
  mainVideoLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

  // Position video label to fill container; re-applied whenever the
  // container resizes (e.g. going fullscreen) via eventFilter below.
  mainVideoLabel_->setGeometry(0, 0, container->width(), container->height());
  container->installEventFilter(this);

  return container;
}

QWidget *ClientWidget::createSidebar() {
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
  secLabel->setStyleSheet(
      "color: rgba(150, 150, 150, 255); font-family: 'Courier New', monospace; "
      "font-size: 10px; letter-spacing: 2px;");
  QLabel *secNameLabel = new QLabel(secondaryName_);
  secNameLabel->setStyleSheet(
      "color: rgba(180, 180, 180, 255); font-family: 'Courier New', monospace; "
      "font-size: 10px; letter-spacing: 2px;");
  secHeaderLayout->addWidget(secLabel);
  secHeaderLayout->addStretch();
  secHeaderLayout->addWidget(secNameLabel);
  secVideoLayout->addLayout(secHeaderLayout);

  // Secondary video preview
  secondaryVideoLabel_ = new QLabel();
  secondaryVideoLabel_->setFixedHeight(
      170); // 16:9 aspect ratio for 304px width
  secondaryVideoLabel_->setStyleSheet(
      "background-color: #1e1f21; "
      "border: 1px solid #484848; "
      "background-image: repeating-linear-gradient(135deg, #363738 0px, "
      "#363738 2px, #282829 2px, #282829 12px);");
  secondaryVideoLabel_->setScaledContents(true);
  secondaryVideoLabel_->setAlignment(Qt::AlignCenter);
  secondaryVideoLabel_->setCursor(Qt::PointingHandCursor);
  secondaryVideoLabel_->installEventFilter(this);

  secVideoLayout->addWidget(secondaryVideoLabel_);

  // Swap button
  swapButton_ = new QPushButton("SWITCH CAMERA FEED");
  swapButton_->setStyleSheet("QPushButton { "
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
                             "}");
  connect(swapButton_, &QPushButton::clicked, this, &ClientWidget::swapVideos);
  secVideoLayout->addWidget(swapButton_);

  layout->addLayout(secVideoLayout);

  // --- TRACKING MODE SECTION ---
  QVBoxLayout *trackingLayout = new QVBoxLayout();
  trackingLayout->setSpacing(12);

  QLabel *trackingHeaderLabel = new QLabel("TRACKING");
  trackingHeaderLabel->setStyleSheet(
      "color: rgba(150, 150, 150, 255); font-family: 'Courier New', monospace; "
      "font-size: 10px; letter-spacing: 2px;");
  trackingLayout->addWidget(trackingHeaderLabel);

  trackingButton_ = new QPushButton("AUTOMATIC");
  trackingButton_->setStyleSheet("QPushButton { "
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
                                 "}");
  connect(trackingButton_, &QPushButton::clicked, this,
          &ClientWidget::toggleTrackingMode);
  trackingLayout->addWidget(trackingButton_);

  layout->addLayout(trackingLayout);

  // --- EXPOSURE CONTROL SECTION ---
  QVBoxLayout *expLayout = new QVBoxLayout();
  expLayout->setSpacing(12);

  // Exposure header
  QLabel *expHeaderLabel = new QLabel("EXPOSURE");
  expHeaderLabel->setStyleSheet(
      "color: rgba(150, 150, 150, 255); font-family: 'Courier New', monospace; "
      "font-size: 10px; letter-spacing: 2px;");
  expLayout->addWidget(expHeaderLabel);

  QString buttonStyle = "QPushButton { "
                        "  background-color: transparent; "
                        "  border: 1px solid #4d4d4d; "
                        "  color: rgba(210, 210, 210, 255); "
                        "  font-family: 'Courier New', monospace; "
                        "  font-size: 14px; "
                        "  padding: 10px; "
                        "} "
                        "QPushButton:hover { "
                        "  background-color: #3d3e40; "
                        "}";

  // Camera 1 controls
  QLabel *cam1Label = new QLabel("Camera 1");
  cam1Label->setStyleSheet(
      "color: rgba(180, 180, 180, 255); font-family: 'Courier New', monospace; "
      "font-size: 11px; letter-spacing: 1px;");
  expLayout->addWidget(cam1Label);

  QHBoxLayout *cam1Layout = new QHBoxLayout();
  QPushButton *cam1DecButton = new QPushButton("−");
  cam1DecButton->setStyleSheet(buttonStyle);
  connect(cam1DecButton, &QPushButton::clicked, this,
          [this]() { sendCommand("dec_exp:1"); });

  QPushButton *cam1IncButton = new QPushButton("+");
  cam1IncButton->setStyleSheet(buttonStyle);
  connect(cam1IncButton, &QPushButton::clicked, this,
          [this]() { sendCommand("inc_exp:1"); });

  cam1Layout->addWidget(cam1DecButton);
  cam1Layout->addWidget(cam1IncButton);
  expLayout->addLayout(cam1Layout);

  // Camera 2 controls
  QLabel *cam2Label = new QLabel("Camera 2");
  cam2Label->setStyleSheet(
      "color: rgba(180, 180, 180, 255); font-family: 'Courier New', monospace; "
      "font-size: 11px; letter-spacing: 1px;");
  expLayout->addWidget(cam2Label);

  QHBoxLayout *cam2Layout = new QHBoxLayout();
  QPushButton *cam2DecButton = new QPushButton("−");
  cam2DecButton->setStyleSheet(buttonStyle);
  connect(cam2DecButton, &QPushButton::clicked, this,
          [this]() { sendCommand("dec_exp:2"); });

  QPushButton *cam2IncButton = new QPushButton("+");
  cam2IncButton->setStyleSheet(buttonStyle);
  connect(cam2IncButton, &QPushButton::clicked, this,
          [this]() { sendCommand("inc_exp:2"); });

  cam2Layout->addWidget(cam2DecButton);
  cam2Layout->addWidget(cam2IncButton);
  expLayout->addLayout(cam2Layout);

  layout->addLayout(expLayout);

  // --- SHOOT BUTTON ---
  QPushButton *shootButton = new QPushButton("SHOOT");
  shootButton->setStyleSheet("QPushButton { "
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
                             "}");
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
  QLabel *oldMain = mainTarget_.exchange(secondaryTarget_);
  secondaryTarget_.store(oldMain);

  // Swap names
  std::swap(mainName_, secondaryName_);
}

void ClientWidget::toggleTrackingMode() {
  manualMode_ = !manualMode_;
  trackingButton_->setText(manualMode_ ? "MANUAL" : "AUTOMATIC");
  sendCommand(manualMode_ ? "mode:manual" : "mode:auto");
}

void ClientWidget::keyPressEvent(QKeyEvent *event) {
  // Movement keys only apply in manual mode. Unmodified keys only, so that
  // shortcuts like Ctrl+L don't move the camera. Auto-repeat is kept so holding
  // a key keeps sending.
  if (manualMode_ && event->modifiers() == Qt::NoModifier) {
    switch (event->key()) {
    case Qt::Key_H:
      sendCommand("move:left");
      return;
    case Qt::Key_J:
      sendCommand("move:down");
      return;
    case Qt::Key_K:
      sendCommand("move:up");
      return;
    case Qt::Key_L:
      sendCommand("move:right");
      return;
    default:
      break;
    }
  }
  QWidget::keyPressEvent(event);
}

void ClientWidget::sendCommand(const QString &command) {
  QByteArray cmdBytes = command.toUtf8();
  ssize_t sent =
      sendto(cmdSocket_, cmdBytes.constData(), cmdBytes.size(), MSG_CONFIRM,
             (const struct sockaddr *)&cmdAddr_, sizeof(cmdAddr_));

  if (sent < 0) {
    qWarning() << "Failed to send command:" << command;
  } else {
    qDebug() << "Command sent:" << command;
  }
}

bool ClientWidget::eventFilter(QObject *obj, QEvent *event) {
  if (obj == secondaryVideoLabel_ &&
      event->type() == QEvent::MouseButtonRelease) {
    swapVideos();
    return true;
  }
  if (event->type() == QEvent::Resize) {
    auto *container = qobject_cast<QWidget *>(obj);
    if (container && container == mainVideoLabel_->parentWidget()) {
      mainVideoLabel_->setGeometry(0, 0, container->width(),
                                   container->height());
    }
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
  painter.drawLine(centerX - halfSize, centerY, centerX + halfSize,
                   centerY); // Horizontal
  painter.drawLine(centerX, centerY - halfSize, centerX,
                   centerY + halfSize); // Vertical

  // Draw corner brackets
  painter.setPen(reticlePen);
  const int cornerSize = 22;

  // Top-left
  painter.drawLine(centerX - halfSize, centerY - halfSize,
                   centerX - halfSize + cornerSize, centerY - halfSize);
  painter.drawLine(centerX - halfSize, centerY - halfSize, centerX - halfSize,
                   centerY - halfSize + cornerSize);

  // Top-right
  painter.drawLine(centerX + halfSize, centerY - halfSize,
                   centerX + halfSize - cornerSize, centerY - halfSize);
  painter.drawLine(centerX + halfSize, centerY - halfSize, centerX + halfSize,
                   centerY - halfSize + cornerSize);

  // Bottom-left
  painter.drawLine(centerX - halfSize, centerY + halfSize,
                   centerX - halfSize + cornerSize, centerY + halfSize);
  painter.drawLine(centerX - halfSize, centerY + halfSize, centerX - halfSize,
                   centerY + halfSize - cornerSize);

  // Bottom-right
  painter.drawLine(centerX + halfSize, centerY + halfSize,
                   centerX + halfSize - cornerSize, centerY + halfSize);
  painter.drawLine(centerX + halfSize, centerY + halfSize, centerX + halfSize,
                   centerY + halfSize - cornerSize);

  // Draw center circle
  painter.drawEllipse(QPoint(centerX, centerY), 23, 23);
}

void ClientWidget::startPipeline(const QString &host, quint16 port,
                                 bool isMain) {
  QString pipelineDesc =
      QString("udpsrc port=%1 ! "
              "application/x-rtp,encoding-name=H264,payload=96 ! "
              "rtph264depay ! avdec_h264 ! videoconvert ! "
              "video/x-raw,format=BGR ! appsink name=sink")
          .arg(port);

  GError *error = nullptr;
  GstElement *pipeline =
      gst_parse_launch(pipelineDesc.toUtf8().constData(), &error);
  if (!pipeline) {
    qWarning() << "Failed to create pipeline:"
               << (error ? error->message : "Unknown");
    if (error)
      g_error_free(error);
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

  if (gst_element_set_state(pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
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
  auto *self = static_cast<ClientWidget *>(user_data);
  GstSample *sample = gst_app_sink_pull_sample(sink);
  if (!sample)
    return GST_FLOW_OK;

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
  bool isMainStream =
      (GST_OBJECT_PARENT(sink) == GST_OBJECT(self->mainPipeline_.get()));

  QMetaObject::invokeMethod(self, "updateVideoFrame", Qt::QueuedConnection,
                            Q_ARG(bool, isMainStream), Q_ARG(QPixmap, pixmap));

  return GST_FLOW_OK;
}

void ClientWidget::updateVideoFrame(bool isMainStream, QPixmap pixmap) {
  QLabel *target = isMainStream ? mainTarget_.load() : secondaryTarget_.load();
  if (!target)
    return;

  // The big main display preserves aspect ratio (letterboxed) instead of
  // stretching to fill; the small sidebar preview keeps its fixed box.
  if (target == mainVideoLabel_) {
    target->setPixmap(pixmap.scaled(target->size(), Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation));
  } else {
    target->setPixmap(pixmap);
  }
}
