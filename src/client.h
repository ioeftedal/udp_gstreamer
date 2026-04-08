#pragma once

#include <QWidget>
#include <QLabel>
#include <QMutex>
#include <QPixmap>
#include <QAtomicInt>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <memory>
#include <atomic>

/**
 * @brief ClientWidget displays two live video streams: main and inset.
 *
 * This widget is responsible for:
 *  - Creating and managing GStreamer pipelines for each stream
 *  - Rendering the streams into QLabel widgets
 *  - Allowing user to swap main/inset streams interactively
 *
 * Thread safety:
 *  - QLabel targets are stored as std::atomic<QLabel*> to safely update pixmaps
 *    from GStreamer's callback threads.
 *  - Layout updates are protected with QMutex.
 */
class ClientWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a ClientWidget with two streams.
     * @param hostMain Host for the main stream.
     * @param portMain Port for the main stream.
     * @param hostInset Host for the inset stream.
     * @param portInset Port for the inset stream.
     * @param parent Optional parent widget.
     *
     * Sets up UI labels, installs event filters for interactivity, and starts
     * GStreamer pipelines immediately.
     */
    explicit ClientWidget(const QString &hostMain, quint16 portMain,
                          const QString &hostInset, quint16 portInset,
                          QWidget *parent = nullptr);

    ~ClientWidget() override;

protected:
    /// Resize event to adjust inset layout dynamically.
    void resizeEvent(QResizeEvent *event) override;

    /// Event filter to detect clicks on the inset label for swapping.
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    /// Initialize and start a GStreamer pipeline for a stream.
    void startPipeline(const QString &host, quint16 port, bool isMain);

    /// Callback from GStreamer when a new sample/frame is available.
    static GstFlowReturn onNewSample(GstAppSink *appsink, gpointer user_data);

    /// Compute and position the inset video dynamically.
    void layoutInset();

    /// Swap the main and inset streams.
    void swapVideos();

    // --- UI elements ---
    QLabel *mainLabel_; ///< Label showing the main stream
    QLabel *insetLabel_; ///< Label showing the inset stream

    // Atomic QLabel targets for thread-safe updates from GStreamer callbacks
    std::atomic<QLabel*> mainTarget_;
    std::atomic<QLabel*> insetTarget_;

    // --- GStreamer pipeline management ---
    struct GstPipelineDeleter {
        /// Properly stops and unrefs a pipeline on destruction
        void operator()(GstElement *e) {
            if (e) {
                gst_element_set_state(e, GST_STATE_NULL);
                gst_object_unref(e);
            }
        }
    };
    using GstPipelinePtr = std::unique_ptr<GstElement, GstPipelineDeleter>;
    GstPipelinePtr mainPipeline_;
    GstPipelinePtr insetPipeline_;

    QMutex layoutMutex_; ///< Protects layoutInset() from concurrent resize calls
};
