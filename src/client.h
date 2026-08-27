#pragma once

#include <QWidget>
#include <QLabel>
#include <QMutex>
#include <QPixmap>
#include <QAtomicInt>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <memory>
#include <atomic>
#include <netinet/in.h>

/**
 * @brief ClientWidget displays a tactical C2-style console with dual video streams.
 *
 * Features:
 *  - Main video feed with tactical overlays (crosshair reticle, corner info)
 *  - Sidebar with secondary video preview, controls, and log
 *  - Dark military/tactical aesthetic
 *  - Interactive stream swapping
 *
 * Thread safety:
 *  - QLabel targets are stored as std::atomic<QLabel*> to safely update pixmaps
 *    from GStreamer's callback threads.
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
     * @param cmdSocket Socket file descriptor for sending commands.
     * @param cmdAddr Server address for sending commands.
     * @param parent Optional parent widget.
     *
     * Sets up UI labels, installs event filters for interactivity, and starts
     * GStreamer pipelines immediately.
     */
    explicit ClientWidget(const QString &hostMain, quint16 portMain,
                          const QString &hostInset, quint16 portInset,
                          int cmdSocket, const struct sockaddr_in &cmdAddr,
                          QWidget *parent = nullptr);

    ~ClientWidget() override;

    /// Applies a decoded frame to the appropriate label on the GUI thread.
    Q_INVOKABLE void updateVideoFrame(bool isMainStream, QPixmap pixmap);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    /// Initialize and start a GStreamer pipeline for a stream.
    void startPipeline(const QString &host, quint16 port, bool isMain);

    /// Callback from GStreamer when a new sample/frame is available.
    static GstFlowReturn onNewSample(GstAppSink *appsink, gpointer user_data);

    /// Swap the main and secondary streams.
    void swapVideos();

    /// Adjust exposure value.
    void adjustExposure(int delta);

    /// Send command to server via UDP.
    void sendCommand(const QString &command);

    /// Create the main video view with tactical overlays.
    QWidget* createMainVideoView();

    /// Create the sidebar with controls.
    QWidget* createSidebar();

    // --- UI elements ---
    QLabel *mainVideoLabel_;      ///< Main video stream display
    QLabel *secondaryVideoLabel_; ///< Secondary video preview
    QPushButton *swapButton_;     ///< Button to swap video feeds
    QPushButton *expUpButton_;    ///< Increase exposure
    QPushButton *expDownButton_;  ///< Decrease exposure
    QLabel *exposureLabel_;       ///< Shows current exposure value

    // Atomic QLabel targets for thread-safe updates from GStreamer callbacks
    std::atomic<QLabel*> mainTarget_;
    std::atomic<QLabel*> secondaryTarget_;

    // --- State ---
    int exposureValue_;           ///< Current exposure compensation (-20 to +20)
    int shotCount_;               ///< Shot counter
    QString mainName_;            ///< Name of main camera
    QString secondaryName_;       ///< Name of secondary camera

    // --- Command socket ---
    int cmdSocket_;               ///< Socket file descriptor for commands
    struct sockaddr_in cmdAddr_;  ///< Server address for commands

    // --- GStreamer pipeline management ---
    struct GstPipelineDeleter {
        void operator()(GstElement *e) {
            if (e) {
                gst_element_set_state(e, GST_STATE_NULL);
                gst_object_unref(e);
            }
        }
    };
    using GstPipelinePtr = std::unique_ptr<GstElement, GstPipelineDeleter>;
    GstPipelinePtr mainPipeline_;
    GstPipelinePtr secondaryPipeline_;
};
