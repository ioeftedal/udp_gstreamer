// server.cpp
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <iostream>
#include <thread>
#include <string>
#include <cstdio>
#include <memory>
#include <array>

// Find a camera device by name
std::string find_camera_device(const std::string &cam_name) {
    for (int i = 0; i < 10; ++i) {
        std::string path = "/dev/video" + std::to_string(i);
        FILE* f = fopen(path.c_str(), "r");
        if (!f) continue;
        fclose(f);

        std::string cmd = "v4l2-ctl -d " + path + " --all 2>/dev/null | grep 'Card type'";
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) continue;
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) result += buffer.data();

        if (result.find(cam_name) != std::string::npos) return path;
    }
    return "";
}

// Start streaming one camera to a UDP port with specified resolution
void start_stream(const std::string &cam_name, const std::string &host, int port,
                  int width, int height, int fps) {
    std::string device = find_camera_device(cam_name);
    if (device.empty()) {
        std::cerr << "Camera '" << cam_name << "' not found!" << std::endl;
        return;
    }

    gst_init(nullptr, nullptr);

    std::string pipeline_desc =
        "v4l2src device=" + device +
        " ! image/jpeg,width=" + std::to_string(width) +
        ",height=" + std::to_string(height) +
        ",framerate=" + std::to_string(fps) + "/1 ! "
        "jpegparse ! avdec_mjpeg ! videoconvert ! "
        "x264enc tune=zerolatency bitrate=5000 speed-preset=superfast ! "
        "rtph264pay ! udpsink host=" + host + " port=" + std::to_string(port);

    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(pipeline_desc.c_str(), &error);
    if (!pipeline) {
        std::cerr << "Failed to create pipeline for " << cam_name
                  << ": " << (error ? error->message : "Unknown error") << std::endl;
        if (error) g_error_free(error);
        return;
    }

    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline for " << cam_name << std::endl;
        gst_object_unref(pipeline);
        return;
    }

    std::cout << "Streaming '" << cam_name << "' on " << host << ":" << port
              << " at " << width << "x" << height << "@" << fps << "fps" << std::endl;

    GstBus* bus = gst_element_get_bus(pipeline);
    bool running = true;
    while (running) {
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (msg) {
            GError* err;
            gchar* debug_info;
            switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ERROR:
                    gst_message_parse_error(msg, &err, &debug_info);
                    std::cerr << "Error on '" << cam_name << "': " << err->message << std::endl;
                    g_error_free(err);
                    g_free(debug_info);
                    running = false;
                    break;
                case GST_MESSAGE_EOS:
                    std::cout << cam_name << " reached EOS" << std::endl;
                    running = false;
                    break;
                default:
                    break;
            }
            gst_message_unref(msg);
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <port0> <port1>" << std::endl;
        return 1;
    }

    std::string host = argv[1];
    int port0 = std::stoi(argv[2]);
    int port1 = std::stoi(argv[3]);

    // Camera names
    std::string cam0 = "Integrated Camera"; // high-res
    std::string cam1 = "Logi C270 HD WebCam"; // lower-res

    // Start two threads with different resolutions
    std::thread t0(start_stream, cam0, host, port0, 1920, 1080, 30);
    std::thread t1(start_stream, cam1, host, port1, 1280, 720, 30);

    t0.join();
    t1.join();

    return 0;
}
