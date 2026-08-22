cmake_minimum_required(VERSION 3.20)
project(PATRONUS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0)

set(CMAKE_AUTOMOC_INCLUDE_DIRECTORIES ${GST_INCLUDE_DIRS})

add_executable(client
    src/client_main.cpp
    src/client.cpp
    src/client.h
)

target_include_directories(client PRIVATE ${GST_INCLUDE_DIRS})
target_link_directories(client PRIVATE ${GST_LIBRARY_DIRS})
target_link_libraries(client PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    ${GST_LIBRARIES}
)

add_executable(server
    src/server_main.cpp
    src/server.cpp
    src/server.h
)

target_include_directories(server PRIVATE ${GST_INCLUDE_DIRS})
target_link_directories(server PRIVATE ${GST_LIBRARY_DIRS})
target_link_libraries(server PRIVATE
    ${GST_LIBRARIES}
)
