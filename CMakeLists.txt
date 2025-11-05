cmake_minimum_required(VERSION 3.16)
project(aes67-nmos-receiver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(AVAHI REQUIRED avahi-client)

find_package(Boost REQUIRED COMPONENTS system thread filesystem)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)

# nmos-cpp (assumed to be installed)
find_library(NMOS_CPP_LIB nmos-cpp REQUIRED)
find_path(NMOS_CPP_INCLUDE nmos REQUIRED)

# Source files
set(SOURCES
    src/main.cpp
    src/aes67_receiver.cpp
    src/pipewire_bridge.cpp
    src/nmos_node.cpp
    src/config_manager.cpp
    src/logger.cpp
)

# Create executable
add_executable(aes67-receiver ${SOURCES})

# Include directories
target_include_directories(aes67-receiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_APP_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${AVAHI_INCLUDE_DIRS}
    ${NMOS_CPP_INCLUDE}
    ${Boost_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(aes67-receiver PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
    ${AVAHI_LIBRARIES}
    ${NMOS_CPP_LIB}
    ${Boost_LIBRARIES}
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
)

# Installation
install(TARGETS aes67-receiver DESTINATION /usr/local/bin)
install(DIRECTORY config/ DESTINATION /etc/aes67-receiver)
install(FILES systemd/aes67-receiver.service DESTINATION /etc/systemd/system)