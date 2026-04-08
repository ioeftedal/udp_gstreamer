#include "server.h"
#include <iostream>
#include <thread>

CameraStreamer::CameraStreamer(const CameraConfig &config)
    : config_(config),
      pipeline_(nullptr, [](GstElement* e){
          // Ensure GStreamer pipeline is properly released
          if(e) { 
              gst_element_set_state(e, GST_STATE_NULL); 
              gst_object_unref(e); 
          }
      }),
      running_(false)
{
    // Defer actual streaming to `start()` to allow flexible object construction.
}

CameraStreamer::~CameraStreamer() {
    stop();
}

void CameraStreamer::start() {
    running_ = true;
    // Launch streaming loop in a separate thread for non-blocking operation
    thread_ = std::thread(&CameraStreamer::run, this);
}

void CameraStreamer::stop() {
    running_ = false;
    cv_.notify_all(); // Wake thread if waiting for device
    if(thread_.joinable()) thread_.join();
}

std::string CameraStreamer::findCameraDevice() const {
    // Search /dev/video0..9 for a camera matching the configured name.
    for(int i = 0; i < 10; ++i) {
        std::string path = "/dev/video" + std::to_string(i);
        if(!std::filesystem::exists(path)) continue;

        std::ifstream f("/sys/class/video4linux/video" + std::to_string(i) + "/name");
        if(!f.is_open()) continue;

        std::string name;
        std::getline(f, name);

        // Return first device that contains the configured name
        if(name.find(config_.name) != std::string::npos) return path;
    }
    return "";
}

void CameraStreamer::run() {
    while(running_) {
        std::string device = findCameraDevice();
        if(device.empty()) {
            std::cerr << "[CameraStreamer] Camera '" << config_.name 
                      << "' not found. Retrying in 2s...\n";
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait_for(lk, std::chrono::seconds(2));
            continue;
        }

        // Construct GStreamer pipeline string dynamically based on configuration.
        std::string pipeline_desc =
            "v4l2src device=" + device +
            " ! image/jpeg,width=" + std::to_string(config_.width) +
            ",height=" + std::to_string(config_.height) +
            ",framerate=" + std::to_string(config_.fps) + "/1 ! "
            "jpegparse ! avdec_mjpeg ! videoconvert ! "
            "x264enc tune=zerolatency bitrate=" + std::to_string(config_.bitrate) +
            " speed-preset=superfast ! rtph264pay ! udpsink host=" + config_.host +
            " port=" + std::to_string(config_.port);

        GError* error = nullptr;
        GstElement* pipeline = gst_parse_launch(pipeline_desc.c_str(), &error);
        if(!pipeline) {
            std::cerr << "[CameraStreamer] Failed to create pipeline for " 
                      << config_.name << ": " << (error ? error->message : "Unknown") << "\n";
            if(error) g_error_free(error);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        pipeline_.reset(pipeline);

        if(gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            std::cerr << "[CameraStreamer] Failed to start pipeline for " << config_.name << "\n";
            pipeline_.reset();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        std::cout << "[CameraStreamer] Streaming '" << config_.name << "' on "
                  << config_.host << ":" << config_.port << "\n";

        // Monitor the pipeline bus for errors or end-of-stream
        GstBus* bus = gst_element_get_bus(pipeline);
        while(running_) {
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, 500 * GST_MSECOND,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            if(msg) {
                GError* err;
                gchar* debug_info;
                switch(GST_MESSAGE_TYPE(msg)) {
                    case GST_MESSAGE_ERROR:
                        gst_message_parse_error(msg, &err, &debug_info);
                        std::cerr << "[CameraStreamer] Error on '" << config_.name
                                  << "': " << err->message << "\n";
                        g_error_free(err);
                        g_free(debug_info);
                        running_ = false;
                        break;
                    case GST_MESSAGE_EOS:
                        std::cerr << "[CameraStreamer] EOS received for " << config_.name << "\n";
                        running_ = false;
                        break;
                    default:
                        break;
                }
                gst_message_unref(msg);
            }
        }

        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline_.reset();

        // Retry after a short delay if still running
        if(running_) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
