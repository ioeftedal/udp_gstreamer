#include <iostream>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>
#include "server.h"
#include <gst/gst.h>

int main() {
    gst_init(nullptr, nullptr); // Initialize GStreamer library

    std::ifstream file("config.json");
    if(!file.is_open()) {
        std::cerr << "Cannot open config.json\n";
        return 1;
    }

    nlohmann::json root;
    file >> root;

    auto cams = root["server"]["cameras"];
    if(!cams.is_array() || cams.empty()) {
        std::cerr << "No cameras defined in config!\n";
        return 1;
    }

    // Use pointers for polymorphic cleanup; alternatively could use unique_ptr
    std::vector<CameraStreamer*> streamers;
    for(auto &cam : cams) {
        CameraConfig cfg{
            cam["name"].get<std::string>(),
            cam["host"].get<std::string>(),
            cam["port"].get<int>(),
            cam["width"].get<int>(),
            cam["height"].get<int>(),
            cam["fps"].get<int>(),
            cam["bitrate"].get<int>()
        };
        auto streamer = new CameraStreamer(cfg);
        streamer->start(); // Non-blocking
        streamers.push_back(streamer);
    }

    std::cout << "[Camera Server] Press Ctrl+C to stop...\n";
    while(true) std::this_thread::sleep_for(std::chrono::seconds(1));

    for(auto s : streamers) { s->stop(); delete s; }
    return 0;
}
