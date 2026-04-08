#include "client.h"
#include <QVBoxLayout>
#include <QMouseEvent>
#include <iostream>

ClientWidget::ClientWidget(const QString &hostMain, quint16 portMain,
                           const QString &hostInset, quint16 portInset,
                           QWidget *parent)
    : QWidget(parent),
      mainLabel_(new QLabel(this)),
      insetLabel_(new QLabel(this)),
      mainPipeline_(nullptr),
      insetPipeline_(nullptr)
{
    mainLabel_->setScaledContents(true);
    mainLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    mainLabel_->setMinimumSize(0, 0);
    insetLabel_->setScaledContents(true);
    insetLabel_->setStyleSheet("border: 2px solid white; background-color: black;");
    insetLabel_->installEventFilter(this);
    insetLabel_->setCursor(Qt::PointingHandCursor);
    insetLabel_->raise();

    mainTarget_ = mainLabel_;
    insetTarget_ = insetLabel_;

    // Main layout only contains the main feed
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->addWidget(mainLabel_);
    setLayout(layout);

    // Start GStreamer pipelines
    startPipeline(hostMain, portMain, true);    // main video
    startPipeline(hostInset, portInset, false); // inset video

    // Click handler for swapping videos
    insetClickHandler_ = [this]() { swapVideos(); };
}

ClientWidget::~ClientWidget() {
    if (mainPipeline_) {
        gst_element_set_state(mainPipeline_, GST_STATE_NULL);
        gst_object_unref(mainPipeline_);
    }
    if (insetPipeline_) {
        gst_element_set_state(insetPipeline_, GST_STATE_NULL);
        gst_object_unref(insetPipeline_);
    }
}

bool ClientWidget::eventFilter(QObject *obj, QEvent *event) {
    if (obj == insetLabel_ && event->type() == QEvent::MouseButtonRelease) {
        if (insetClickHandler_) insetClickHandler_();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

// void ClientWidget::swapVideos() {
//     std::swap(mainPixmap_, insetPixmap_);
//     mainLabel_->setPixmap(mainPixmap_);
//     insetLabel_->setPixmap(insetPixmap_);
// }

void ClientWidget::swapVideos() {
    std::swap(mainTarget_, insetTarget_);
}

void ClientWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    layoutInset();
}

void ClientWidget::layoutInset() {
    const QSize availableSize = size();
    constexpr int margin_px = 10;
    constexpr int targetWidth_px = 320;

    int insetWidth = targetWidth_px;
    int insetHeight = insetWidth * 3 / 4; // 4:3 aspect ratio

    if (insetHeight > availableSize.height() - 2*margin_px)
        insetHeight = availableSize.height() - 2*margin_px;

    insetWidth = insetHeight * 4 / 3;

    int insetX = availableSize.width() - insetWidth - margin_px;
    int insetY = availableSize.height() - insetHeight - margin_px;

    insetLabel_->setGeometry(insetX, insetY, insetWidth, insetHeight);
    insetLabel_->raise();
}

void ClientWidget::startPipeline(const QString &host, quint16 port, bool isMain) {
    gst_init(nullptr, nullptr);
    QString pipelineDesc = QString(
        "udpsrc port=%1 ! "
        "application/x-rtp,encoding-name=H264,payload=96 ! "
        "rtph264depay ! avdec_h264 ! videoconvert ! "
        "video/x-raw,format=BGR ! appsink name=sink")
        .arg(port);

    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(pipelineDesc.toUtf8().data(), &error);
    if (!pipeline) {
        std::cerr << "Failed to create pipeline: "
                  << (error ? error->message : "Unknown") << std::endl;
        if (error) g_error_free(error);
        return;
    }

    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    gst_app_sink_set_emit_signals((GstAppSink*)appsink, true);
    gst_app_sink_set_max_buffers((GstAppSink*)appsink, 1);
    gst_app_sink_set_drop((GstAppSink*)appsink, true);
    g_object_set(appsink, "sync", FALSE, nullptr);

    g_signal_connect(appsink, "new-sample", G_CALLBACK(onNewSample), this);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    if (isMain) mainPipeline_ = pipeline;
    else insetPipeline_ = pipeline;
}

GstFlowReturn ClientWidget::onNewSample(GstAppSink *sink, gpointer user_data) {
    ClientWidget *self = static_cast<ClientWidget*>(user_data);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_OK;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    GstStructure *s = gst_caps_get_structure(caps, 0);
    int width, height;
    gst_structure_get_int(s, "width", &width);
    gst_structure_get_int(s, "height", &height);

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    QImage img((uchar*)map.data, width, height, QImage::Format_RGB888);
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    if (img.isNull()) return GST_FLOW_OK;

    QPixmap pixmap = QPixmap::fromImage(img.rgbSwapped());

    // if (self->mainPipeline_ && GST_OBJECT_PARENT(sink) == GST_OBJECT(self->mainPipeline_)) {
    //     self->mainPixmap_ = pixmap;
    //     QMetaObject::invokeMethod(self->mainLabel_, "setPixmap",
    //                               Qt::QueuedConnection,
    //                               Q_ARG(QPixmap, self->mainPixmap_));
    // } else {
    //     self->insetPixmap_ = pixmap;
    //     QMetaObject::invokeMethod(self->insetLabel_, "setPixmap",
    //                               Qt::QueuedConnection,
    //                               Q_ARG(QPixmap, self->insetPixmap_));
    // }

    bool isMainStream =
        (GST_OBJECT_PARENT(sink) == GST_OBJECT(self->mainPipeline_));

    QLabel* target = isMainStream ? self->mainTarget_
                                : self->insetTarget_;

    QMetaObject::invokeMethod(target, "setPixmap",
                            Qt::QueuedConnection,
                            Q_ARG(QPixmap, pixmap));

    return GST_FLOW_OK;
}
