#include "client.h"
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QMetaObject>
#include <QDebug>

ClientWidget::ClientWidget(const QString &hostMain, quint16 portMain,
                           const QString &hostInset, quint16 portInset,
                           QWidget *parent)
    : QWidget(parent),
      mainLabel_(new QLabel(this)),
      insetLabel_(new QLabel(this)),
      mainTarget_(mainLabel_),
      insetTarget_(insetLabel_)
{
    // --- Configure main stream label ---
    mainLabel_->setScaledContents(true);
    mainLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    mainLabel_->setMinimumSize(0, 0); // Allow it to shrink freely

    // --- Configure inset label ---
    insetLabel_->setScaledContents(true);
    insetLabel_->setStyleSheet("border: 2px solid white; background-color: black;");
    insetLabel_->setCursor(Qt::PointingHandCursor);
    insetLabel_->installEventFilter(this); // capture clicks for swapping
    insetLabel_->raise(); // ensure inset is always on top

    // --- Layout main label only; inset is positioned manually ---
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mainLabel_);
    setLayout(layout);

    // --- Start streaming pipelines ---
    startPipeline(hostMain, portMain, true);
    startPipeline(hostInset, portInset, false);
}

ClientWidget::~ClientWidget() = default;

/**
 * @brief Swap main and inset video labels.
 *
 * Uses atomic exchange to safely switch targets that may be updated concurrently
 * by the GStreamer callback thread.
 */
void ClientWidget::swapVideos() {
    QLabel* oldMain = mainTarget_.exchange(insetTarget_);
    insetTarget_.store(oldMain);

    mainLabel_->raise();
    insetLabel_->raise();
}

/**
 * @brief Detect mouse release on inset label to trigger swap.
 */
bool ClientWidget::eventFilter(QObject *obj, QEvent *event) {
    if (obj == insetLabel_ && event->type() == QEvent::MouseButtonRelease) {
        swapVideos();
        return true; // consume event
    }
    return QWidget::eventFilter(obj, event);
}

/**
 * @brief Resize event triggers inset layout recalculation.
 */
void ClientWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    layoutInset();
}

/**
 * @brief Layout inset label as a smaller overlay in bottom-right.
 *
 * Maintains a 4:3 aspect ratio, scaled to 25% of widget width, and
 * ensures inset fits inside the main widget with margin.
 */
void ClientWidget::layoutInset() {
    QMutexLocker locker(&layoutMutex_);
    const QSize availableSize = size();
    constexpr int margin_px = 10;
    constexpr float insetRatio = 0.25f; // 25% width of widget

    int insetWidth = static_cast<int>(availableSize.width() * insetRatio);
    int insetHeight = insetWidth * 3 / 4;

    if (insetHeight > availableSize.height() - 2*margin_px)
        insetHeight = availableSize.height() - 2*margin_px;

    insetWidth = insetHeight * 4 / 3;

    int insetX = availableSize.width() - insetWidth - margin_px;
    int insetY = availableSize.height() - insetHeight - margin_px;

    insetLabel_->setGeometry(insetX, insetY, insetWidth, insetHeight);
    insetLabel_->raise();
}

/**
 * @brief Start a GStreamer pipeline to receive H264 over UDP.
 *
 * Builds the pipeline string dynamically and sets up an appsink callback.
 * Using RAII ensures pipeline is properly cleaned up even on failure.
 */
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
    gst_app_sink_set_max_buffers(GST_APP_SINK(appsink), 1); // prevent lag
    gst_app_sink_set_drop(GST_APP_SINK(appsink), true);
    g_object_set(appsink, "sync", FALSE, nullptr); // render as fast as possible

    // Pass context with bool to identify which stream this is
    StreamContext* context = isMain ? &mainContext_ : &insetContext_;
    g_signal_connect(appsink, "new-sample", G_CALLBACK(onNewSample), context);

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "Failed to set pipeline to PLAYING state";
        gst_object_unref(pipeline);
        return;
    }

    if (isMain) mainPipeline_.reset(pipeline);
    else insetPipeline_.reset(pipeline);
}

/**
 * @brief Called on new frames from GStreamer appsink.
 *
 * Converts GstBuffer to QPixmap and updates QLabel using Qt::QueuedConnection
 * to safely cross threads.
 */
GstFlowReturn ClientWidget::onNewSample(GstAppSink *sink, gpointer user_data) {
    auto *context = static_cast<StreamContext*>(user_data);
    auto *self = context->widget;
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

    // Convert raw BGR buffer to QPixmap (deep copy to avoid lifetime issues)
    QImage img(map.data, width, height, QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(img.rgbSwapped().copy());
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    // Use bool from context to determine target label (same pattern as server)
    QLabel* target = context->isMain ? self->mainTarget_.load() : self->insetTarget_.load();

    // Post update to GUI thread
    QMetaObject::invokeMethod(target, "setPixmap", Qt::QueuedConnection,
                              Q_ARG(QPixmap, pixmap));

    return GST_FLOW_OK;
}
