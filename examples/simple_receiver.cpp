// SPDX-License-Identifier: MIT
/*
 * RPi-AES67 - Simple Receiver Example
 * 
 * Demonstrates basic AES67 stream reception with PipeWire output.
 */

#include <iostream>
#include <csignal>
#include <thread>
#include <atomic>

#include "rpi_aes67/config.h"
#include "rpi_aes67/logger.h"
#include "rpi_aes67/receiver.h"
#include "rpi_aes67/pipewire_io.h"
#include "rpi_aes67/ptp_sync.h"

using namespace rpi_aes67;

static std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    // Initialize logger
    Logger::init("simple_receiver", LogLevel::Info);
    
    // Parse arguments
    std::string multicast_ip = "239.69.1.1";
    uint16_t port = 5004;
    
    if (argc > 1) {
        multicast_ip = argv[1];
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    
    LOG_INFO("Simple AES67 Receiver Example");
    LOG_INFO("Listening on {}:{}", multicast_ip, port);
    
    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // Initialize PipeWire
        if (!PipeWireManager::instance().initialize()) {
            LOG_WARNING("PipeWire not available, audio output disabled");
        }
        
        // Initialize PTP
        auto ptp_sync = std::make_shared<PTPSync>();
        if (ptp_sync->initialize("eth0")) {
            ptp_sync->start();
            LOG_INFO("PTP synchronization started");
        }
        
        // Create receiver
        auto receiver = std::make_shared<AES67Receiver>();
        
        ReceiverConfig config;
        config.id = "simple-receiver";
        config.label = "Simple Receiver";
        config.channels = 2;
        
        AudioProcessingConfig audio_config;
        audio_config.jitter_buffer_ms = 10.0;
        
        if (!receiver->configure(config, audio_config)) {
            LOG_ERROR("Failed to configure receiver");
            return 1;
        }
        
        // Set up audio output
        auto audio_output = std::make_shared<PipeWireOutput>();
        if (audio_output->initialize()) {
            receiver->set_audio_sink(audio_output);
        }
        
        receiver->set_ptp_sync(ptp_sync);
        
        if (!receiver->initialize()) {
            LOG_ERROR("Failed to initialize receiver");
            return 1;
        }
        
        // Connect to stream
        AudioFormat format;
        format.sample_rate = 48000;
        format.channels = 2;
        format.bit_depth = 24;
        
        if (!receiver->connect(multicast_ip, port, format)) {
            LOG_ERROR("Failed to connect to {}:{}", multicast_ip, port);
            return 1;
        }
        
        // Start receiving
        if (!receiver->start()) {
            LOG_ERROR("Failed to start receiver");
            return 1;
        }
        
        LOG_INFO("Receiver started. Press Ctrl+C to stop.");
        
        // Main loop - print statistics
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            auto stats = receiver->get_statistics();
            LOG_INFO("Stats: {} packets received, {} lost, {:.1f} kbps, buffer: {:.0f}%",
                     stats.packets_received,
                     stats.packets_lost,
                     stats.bitrate_kbps,
                     stats.buffer_level * 100.0);
        }
        
        // Cleanup
        LOG_INFO("Stopping receiver...");
        receiver->stop();
        receiver->disconnect();
        ptp_sync->stop();
        PipeWireManager::instance().shutdown();
        
        LOG_INFO("Done.");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error: {}", e.what());
        return 1;
    }
}
