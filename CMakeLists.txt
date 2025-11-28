cmake_minimum_required(VERSION 3.16)
project(rpi-aes67 VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_EXAMPLES "Build example applications" ON)
option(BUILD_TESTS "Build unit tests" OFF)
option(ENABLE_PIPEWIRE "Enable PipeWire audio support" ON)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# Optional PipeWire support
if(ENABLE_PIPEWIRE)
    pkg_check_modules(PIPEWIRE libpipewire-0.3)
    if(PIPEWIRE_FOUND)
        add_compile_definitions(HAVE_PIPEWIRE)
        message(STATUS "PipeWire support enabled")
    else()
        message(WARNING "PipeWire not found, audio I/O will be disabled")
    endif()
endif()

# Core library sources
set(RPI_AES67_SOURCES
    src/config.cpp
    src/logger.cpp
    src/ptp_sync.cpp
    src/pipewire_io.cpp
    src/sender.cpp
    src/receiver.cpp
    src/nmos_node.cpp
)

# Create static library
add_library(rpi_aes67 STATIC ${RPI_AES67_SOURCES})

target_include_directories(rpi_aes67 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rpi_aes67 PUBLIC
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(PIPEWIRE_FOUND)
    target_include_directories(rpi_aes67 PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
    target_link_libraries(rpi_aes67 PRIVATE ${PIPEWIRE_LIBRARIES})
endif()

# Main executable
add_executable(rpi-aes67 src/main.cpp)

target_link_libraries(rpi-aes67 PRIVATE
    rpi_aes67
)

# Example applications
if(BUILD_EXAMPLES)
    # Simple receiver example
    add_executable(simple_receiver examples/simple_receiver.cpp)
    target_link_libraries(simple_receiver PRIVATE rpi_aes67)
    
    # Simple sender example  
    add_executable(simple_sender examples/simple_sender.cpp)
    target_link_libraries(simple_sender PRIVATE rpi_aes67)
    
    # Bidirectional example
    add_executable(bidirectional examples/bidirectional.cpp)
    target_link_libraries(bidirectional PRIVATE rpi_aes67)
endif()

# Unit tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# Installation
install(TARGETS rpi-aes67 rpi_aes67
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/rpi_aes67
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY config/
    DESTINATION /etc/rpi-aes67
)

install(FILES 
    systemd/aes67-sender.service
    systemd/aes67-receiver.service
    systemd/aes67-bidirectional.service
    DESTINATION /etc/systemd/system
)

# Print configuration summary
message(STATUS "")
message(STATUS "=== RPi-AES67 Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "PipeWire: ${PIPEWIRE_FOUND}")
message(STATUS "Build Examples: ${BUILD_EXAMPLES}")
message(STATUS "Build Tests: ${BUILD_TESTS}")
message(STATUS "")