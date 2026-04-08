#pragma once
#include <string>
#include <vector>
#include <memory>
#include <gst/gst.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include <fstream>

/**
 * @brief Configuration parameters for a camera.
 *
 * Contains all the settings needed to initialize and stream a camera feed.
 * This separates configuration from logic, making the streamer reusable.
 */
struct CameraConfig {
    std::string name;   ///< Logical camera name (used to match device)
    std::string host;   ///< IP address or hostname to stream to
    int port;           ///< UDP port for streaming
    int width;          ///< Video width in pixels
    int height;         ///< Video height in pixels
    int fps;            ///< Frame rate
    int bitrate;        ///< x264 encoder bitrate (kbps)
};

/**
 * @brief Handles streaming video from a camera to a remote host using GStreamer.
 *
 * Encapsulates the GStreamer pipeline lifecycle and device discovery.
 * Streaming is performed in a dedicated thread to allow non-blocking operation.
 */
class CameraStreamer {
public:
    /**
     * @brief Constructs a CameraStreamer with a given configuration.
     * @param config CameraConfig with parameters for streaming.
     *
     * Initializes internal state and sets up a smart pointer for GStreamer cleanup.
     */
    CameraStreamer(const CameraConfig &config);

    /**
     * @brief Destructor ensures the streaming thread is stopped and resources released.
     */
    ~CameraStreamer();

    /**
     * @brief Starts streaming in a separate thread.
     * 
     * This allows non-blocking streaming and automatic reconnection if the device is temporarily unavailable.
     */
    void start();

    /**
     * @brief Stops streaming gracefully.
     *
     * Signals the streaming thread to exit, waits for thread completion,
     * and ensures the GStreamer pipeline is properly stopped.
     */
    void stop();

private:
    /**
     * @brief Main streaming loop executed in a separate thread.
     *
     * Handles device detection, pipeline creation, error handling, and reconnection.
     */
    void run();

    /**
     * @brief Finds the device path for the camera using its configured name.
     * @return Device path like "/dev/video0" or empty string if not found.
     *
     * This abstracts OS-specific logic for device detection and allows retrying.
     */
    std::string findCameraDevice() const;

private:
    CameraConfig config_;  ///< Camera configuration
    std::unique_ptr<GstElement, void(*)(GstElement*)> pipeline_; ///< Smart pointer managing GStreamer pipeline
    std::thread thread_;   ///< Thread running the streaming loop
    std::atomic<bool> running_; ///< Flag to control thread lifecycle
    std::mutex mutex_;     ///< Synchronization for condition variable
    std::condition_variable cv_; ///< Used for periodic retries when device is unavailable
};
