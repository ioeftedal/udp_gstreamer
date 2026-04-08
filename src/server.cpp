#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <iostream>
#include <thread>
#include <vector>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp> // Add nlohmann/json.hpp to your project

struct CameraConfig {
    std::string name;
    std::string host;
    int port;
    int width;
    int height;
    int fps;
    int bitrate;
};

std::string find_camera_device(const std::string &cam_name) {
    for (int i = 0; i < 10; ++i) {
        std::string path = "/dev/video" + std::to_string(i);
        FILE* f = fopen(path.c_str(), "r");
        if (!f) continue;
        fclose(f);

        std::string cmd = "v4l2-ctl -d " + path + " --all 2>/dev/null | grep 'Card type'";
        char buffer[128];
        std::string result;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) continue;
        while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
        pclose(pipe);

        if (result.find(cam_name) != std::string::npos) return path;
    }
    return "";
}

void start_stream(const CameraConfig &cam) {
    std::string device = find_camera_device(cam.name);
    if (device.empty()) {
        std::cerr << "Camera '" << cam.name << "' not found!\n";
        return;
    }

    gst_init(nullptr, nullptr);

    std::string pipeline_desc =
        "v4l2src device=" + device +
        " ! image/jpeg,width=" + std::to_string(cam.width) +
        ",height=" + std::to_string(cam.height) +
        ",framerate=" + std::to_string(cam.fps) + "/1 ! "
        "jpegparse ! avdec_mjpeg ! videoconvert ! "
        "x264enc tune=zerolatency bitrate=" + std::to_string(cam.bitrate) +
        " speed-preset=superfast ! rtph264pay ! udpsink host=" + cam.host +
        " port=" + std::to_string(cam.port);

    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(pipeline_desc.c_str(), &error);
    if (!pipeline) {
        std::cerr << "Failed to create pipeline for " << cam.name
                  << ": " << (error ? error->message : "Unknown") << "\n";
        if (error) g_error_free(error);
        return;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline for " << cam.name << "\n";
        gst_object_unref(pipeline);
        return;
    }

    std::cout << "Streaming '" << cam.name << "' on " << cam.host << ":" << cam.port
              << " at " << cam.width << "x" << cam.height << "@" << cam.fps
              << "fps bitrate " << cam.bitrate << "\n";

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
                    std::cerr << "Error on '" << cam.name << "': " << err->message << "\n";
                    g_error_free(err);
                    g_free(debug_info);
                    running = false;
                    break;
                case GST_MESSAGE_EOS:
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

int main() {
    std::ifstream file("config.json");
    if (!file.is_open()) {
        std::cerr << "Cannot open config.json\n";
        return 1;
    }

    nlohmann::json root;
    file >> root;

    auto cams = root["server"]["cameras"];
    std::vector<CameraConfig> cameraConfigs;

    for (auto &cam : cams) {
        cameraConfigs.push_back(CameraConfig{
            cam["name"].get<std::string>(),
            cam["host"].get<std::string>(),
            cam["port"].get<int>(),
            cam["width"].get<int>(),
            cam["height"].get<int>(),
            cam["fps"].get<int>(),
            cam["bitrate"].get<int>()
        });
    }

    std::vector<std::thread> threads;
    for (auto &cam : cameraConfigs) {
        threads.emplace_back(start_stream, cam);
    }

    for (auto &t : threads) t.join();

    return 0;
}
