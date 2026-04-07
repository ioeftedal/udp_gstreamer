# Makefile for UDP GStreamer project

BUILD_DIR=build

# Default target
all: build

# Step 1: configure (creates build directory if missing)
$(BUILD_DIR):
	cmake -B $(BUILD_DIR)

# Step 2: build (depends on configure)
build: $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

# Run server
server: build
	./$(BUILD_DIR)/server

# Run client
client: build
	./$(BUILD_DIR)/client

# Run both server and client
run-both: build
	./$(BUILD_DIR)/server & \
	sleep 1 && \
	./$(BUILD_DIR)/client

# Clean everything
clean:
	rm -rf $(BUILD_DIR)
